import datetime
import hashlib
import hmac
import os
import shutil
import threading
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

from .catalog import AGENT

DEFAULT_R2_ENV = Path.home() / ".config/swofty-dev/r2.env"
CHUNK = 1 << 20


class FetchError(Exception):
    pass


def read_env_file(path):
    out = {}
    try:
        text = Path(path).read_text()
    except OSError:
        return out
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, _, v = line.partition("=")
        out[k.strip()] = v.strip().strip('"').strip("'")
    return out


class R2:
    def __init__(self, endpoint, bucket, public_url, access_key, secret_key):
        self.endpoint = endpoint.rstrip("/")
        self.bucket = bucket
        self.public_url = public_url.rstrip("/")
        self.access_key = access_key
        self.secret_key = secret_key
        self._listed = {}

    @classmethod
    def from_env(cls, env_file=DEFAULT_R2_ENV):
        env = dict(read_env_file(env_file))
        env.update({k: v for k, v in os.environ.items() if k.startswith("R2_")})
        need = ("R2_ENDPOINT", "R2_BUCKET", "R2_PUBLIC_URL", "R2_READ_ACCESS_KEY_ID", "R2_READ_SECRET_ACCESS_KEY")
        if not all(env.get(k) for k in need):
            return None
        return cls(
            env["R2_ENDPOINT"],
            env["R2_BUCKET"],
            env["R2_PUBLIC_URL"],
            env["R2_READ_ACCESS_KEY_ID"],
            env["R2_READ_SECRET_ACCESS_KEY"],
        )

    def _signed_get(self, query):
        now = datetime.datetime.now(datetime.timezone.utc)
        amz_date = now.strftime("%Y%m%dT%H%M%SZ")
        date = now.strftime("%Y%m%d")
        host = urllib.parse.urlparse(self.endpoint).netloc
        path = "/" + urllib.parse.quote(self.bucket)
        canonical_query = "&".join(
            f"{urllib.parse.quote(k, safe='-_.~')}={urllib.parse.quote(v, safe='-_.~')}"
            for k, v in sorted(query.items())
        )
        payload = hashlib.sha256(b"").hexdigest()
        headers = {"host": host, "x-amz-content-sha256": payload, "x-amz-date": amz_date}
        signed = ";".join(sorted(headers))
        canonical = "\n".join(
            [
                "GET",
                path,
                canonical_query,
                "".join(f"{k}:{headers[k]}\n" for k in sorted(headers)),
                signed,
                payload,
            ]
        )
        scope = f"{date}/auto/s3/aws4_request"
        to_sign = "\n".join(
            ["AWS4-HMAC-SHA256", amz_date, scope, hashlib.sha256(canonical.encode()).hexdigest()]
        )
        k = ("AWS4" + self.secret_key).encode()
        for part in (date, "auto", "s3", "aws4_request"):
            k = hmac.new(k, part.encode(), hashlib.sha256).digest()
        signature = hmac.new(k, to_sign.encode(), hashlib.sha256).hexdigest()
        auth = (
            f"AWS4-HMAC-SHA256 Credential={self.access_key}/{scope}, "
            f"SignedHeaders={signed}, Signature={signature}"
        )
        req = urllib.request.Request(
            f"{self.endpoint}{path}?{canonical_query}",
            headers={
                "x-amz-content-sha256": payload,
                "x-amz-date": amz_date,
                "Authorization": auth,
                "User-Agent": AGENT,
            },
        )
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.read()

    def list(self, prefix):
        if prefix in self._listed:
            return self._listed[prefix]
        ns = "{http://s3.amazonaws.com/doc/2006-03-01/}"
        out, token = [], None
        while True:
            query = {"list-type": "2", "prefix": prefix}
            if token:
                query["continuation-token"] = token
            root = ET.fromstring(self._signed_get(query))
            for c in root.findall(f"{ns}Contents"):
                out.append((c.findtext(f"{ns}Key"), int(c.findtext(f"{ns}Size") or 0)))
            if root.findtext(f"{ns}IsTruncated") != "true":
                break
            token = root.findtext(f"{ns}NextContinuationToken")
        self._listed[prefix] = out
        return out

    def candidates(self, entry):
        listed = self.list(f"addons/{entry.id}/{entry.version}/")
        return [
            f"{self.public_url}/{urllib.parse.quote(key, safe='/')}"
            for key, size in listed
            if size == entry.size
        ]


def cache_path(cache, entry):
    return Path(cache) / "downloads" / entry.id / entry.version / f"{entry.sha256}.swoftyaddon"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(CHUNK)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def _download(url, dst, entry):
    tmp = dst.with_name(f"{dst.name}.{os.getpid()}.{threading.get_ident()}.part")
    req = urllib.request.Request(url, headers={"User-Agent": AGENT})
    h = hashlib.sha256()
    size = 0
    with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
        while True:
            b = r.read(CHUNK)
            if not b:
                break
            h.update(b)
            size += len(b)
            if size > entry.size:
                break
            f.write(b)
    if size != entry.size or h.hexdigest() != entry.sha256:
        tmp.unlink(missing_ok=True)
        return f"{url}: got {size} bytes sha256 {h.hexdigest()[:12]}, the catalog lists {entry.size} bytes {entry.sha256[:12]}"
    os.replace(tmp, dst)
    return None


def ensure(entry, cache, r2=None, count_downloads=False, min_free=10 << 30):
    dst = cache_path(cache, entry)
    if dst.is_file() and dst.stat().st_size == entry.size:
        return dst
    dst.parent.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(dst.parent).free
    if free - entry.size < min_free:
        raise FetchError(
            f"{entry.label}: {entry.size >> 20} MB would leave less than {min_free >> 30} GB free "
            f"({free >> 30} GB free now)"
        )
    tried = []
    urls = r2.candidates(entry) if r2 is not None else []
    if count_downloads and entry.url:
        urls.append(entry.url)
    if not urls:
        why = "not in the CDN bucket" if r2 is not None else "no R2 read credentials"
        raise FetchError(
            f"{entry.label}: {why}; pass --count-downloads to fetch it through the registry "
            "(each fetch then counts as a download on the Addon Browser)"
        )
    for url in urls:
        try:
            why = _download(url, dst, entry)
        except OSError as e:
            why = f"{url}: {e}"
        if why is None:
            return dst
        tried.append(why)
    raise FetchError(f"{entry.label}: " + "; ".join(tried))
