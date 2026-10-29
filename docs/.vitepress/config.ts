import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { defineConfigWithTheme } from "vitepress";
import { newsFeed } from "../scripts/news.mjs";
import { flashCodeTheme } from "./theme/code-theme";
import type { DocsThemeConfig } from "./theme/types";

const repo = "https://github.com/Swofty-Developments/SwoftyLauncher";
const base = process.env.DOCS_BASE ?? "/";
const site = "https://f1.swofty.net";

const fonts =
  "https://fonts.googleapis.com/css2?family=Open+Sans:wght@400;600;700&family=Poppins:wght@400;600;700&family=Quicksand:wght@600;700&family=Source+Code+Pro:wght@400;500;600&family=Varela+Round&display=swap";

const pickTheme = `try{var t=localStorage.getItem("f1mm-docs-theme");if(t==="dark"||t==="ocean"||t==="peach")document.documentElement.classList.add("flash-theme-"+t)}catch(e){}`;

const pickSidebar = `try{if(localStorage.getItem("f1mm-docs-sidebar")==="closed")document.documentElement.classList.add("flash-sidebar-closed")}catch(e){}`;

const moved: Record<string, string> = {
  "/tutorials/first-addon": "/reference/addons/first-addon",
  "/tutorials/addon-settings": "/reference/addons/settings",
  "/tutorials/hooking": "/reference/addons/hooking",
  "/tutorials/in-game-ui": "/reference/addons/in-game-ui",
  "/tutorials/database": "/reference/data/database",
  "/tutorials/erp-assets": "/reference/data/textures",
  "/modding/template": "/reference/addons/template",
  "/modding/manifest": "/reference/addons/manifest",
  "/modding/native-plugins": "/reference/addons/native-plugins",
  "/modding/database-ops": "/reference/ops/database",
  "/modding/erp-ops": "/reference/ops/erp",
  "/modding/ui-patches": "/reference/ops/ui-patches",
  "/reference/std/files": "/reference/ops/files",
  "/using/mods-and-maps": "/using/addons",
  "/using/importing-maps": "/using/mods",
  "/guide/addons-mods-maps": "/guide/addons-mods-tracks",
  "/reference/maps": "/reference/installing-tracks",
  "/guide/making-a-mod": "/reference/mods/",
  "/guide/making-an-addon": "/reference/addons/",
  "/reference/mods/database": "/reference/data/database",
  "/reference/mods/text": "/reference/data/text",
  "/reference/mods/erp-assets": "/reference/data/textures",
  "/reference/mods/livery": "/reference/data/livery",
  "/reference/mods/packaging": "/reference/addons/publishing",
  "/reference/mods/publishing": "/reference/addons/publishing",
};

export default defineConfigWithTheme<DocsThemeConfig>({
  title: "Swofty Launcher for F1 25",
  description: "Addons and mods for F1 25 on Windows and Linux",
  base,
  lang: "en-AU",
  appearance: "force-dark",
  srcExclude: ["**/public/**", "scripts/**"],
  sitemap: { hostname: site },
  buildEnd({ outDir }) {
    writeFileSync(join(outDir, "news.json"), `${JSON.stringify(newsFeed())}\n`);
    for (const [from, to] of Object.entries(moved)) {
      const url = to.endsWith("/") ? `${base}${to.slice(1)}` : `${base}${to.slice(1)}.html`;
      const file = join(outDir, `${from}.html`);
      mkdirSync(dirname(file), { recursive: true });
      writeFileSync(
        file,
        `<!doctype html><meta charset="utf-8"><title>Moved</title><link rel="canonical" href="${url}"><meta http-equiv="refresh" content="0; url=${url}"><script>location.replace(${JSON.stringify(url)} + location.hash)</script><a href="${url}">${url}</a>\n`,
      );
    }
  },
  head: [
    ["link", { rel: "icon", href: `${base}logo.svg`, type: "image/svg+xml" }],
    ["meta", { name: "theme-color", content: "#2a1b39" }],
    ["meta", { property: "og:site_name", content: "Swofty Launcher for F1 25" }],
    ["meta", { property: "og:image", content: `${site}/screenshots/hero-launcher.png` }],
    ["link", { rel: "preconnect", href: "https://fonts.googleapis.com" }],
    ["link", { rel: "preconnect", href: "https://fonts.gstatic.com", crossorigin: "" }],
    ["link", { rel: "stylesheet", href: fonts }],
    ["script", {}, pickTheme],
    ["script", {}, pickSidebar],
  ],
  markdown: {
    theme: flashCodeTheme,
    lineNumbers: true,
    config(md) {
      const { rules } = md.renderer;
      const code = rules.code_inline!;
      rules.table_open = (tokens, idx, options, env, self) => {
        env.inTable = true;
        return `<div class="table-wrap">${self.renderToken(tokens, idx, options)}`;
      };
      rules.table_close = (tokens, idx, options, env, self) => {
        env.inTable = false;
        return `${self.renderToken(tokens, idx, options)}</div>\n`;
      };
      const cell: typeof code = (tokens, idx, options, _env, self) => {
        const inline = tokens[idx + 1];
        if (inline?.type === "inline" && inline.content.length <= 20) tokens[idx].attrJoin("class", "short");
        return self.renderToken(tokens, idx, options);
      };
      rules.th_open = cell;
      rules.td_open = cell;
      rules.code_inline = (tokens, idx, options, env, self) => {
        if (!env.inTable) return code(tokens, idx, options, env, self);
        const t = tokens[idx];
        const body = md.utils.escapeHtml(t.content).replace(/\.(?=[A-Za-z_])/g, ".<wbr>");
        return `<code${self.renderAttrs(t)}>${body}</code>`;
      };
    },
  },
  themeConfig: {
    logo: "/logo.svg",
    siteTitle: "Swofty Launcher for F1 25",
    repo,
    socialLinks: [{ icon: "github", link: repo }],
    search: {
      provider: "local",
    },
    tabs: [
      {
        id: "users",
        text: "Users",
        icon: "book",
        groups: [
          {
            text: "Getting started",
            items: [
              { text: "Introduction", link: "/guide/" },
              { text: "Addons, mods and tracks", link: "/guide/addons-mods-tracks" },
              { text: "Installation", link: "/guide/installation" },
              { text: "Windows", link: "/guide/windows" },
              { text: "Linux and Steam Deck", link: "/guide/linux" },
            ],
          },
          {
            text: "Using the launcher",
            items: [
              { text: "Overview", link: "/using/" },
              { text: "News", link: "/using/news" },
              { text: "Addons", link: "/using/addons" },
              { text: "Addon Browser", link: "/using/addon-browser" },
              { text: "Swofty's addons", link: "/using/swofty-addons" },
              { text: "Simplified Importer", link: "/using/mods" },
              { text: "Profiles", link: "/using/profiles" },
              { text: "In-game addon settings", link: "/using/swofty-mod" },
              { text: "Steam and EA app copies", link: "/using/game-copies" },
              { text: "Playing in VR", link: "/using/vr" },
              { text: "Career Mode Editor", link: "/using/career-editor" },
              { text: "Driver Manager", link: "/using/driver-manager" },
              { text: "DB Editor", link: "/using/db-editor" },
              { text: "Tools", link: "/using/tools" },
              { text: "Restoring the game", link: "/using/profiles-and-restoring" },
              { text: "Updating", link: "/using/updating" },
              { text: "Sending your logs", link: "/using/send-logs" },
              { text: "Uninstalling", link: "/guide/uninstall" },
            ],
          },
          {
            text: "How it works",
            items: [
              { text: "Launching the game", link: "/reference/launch" },
              { text: "Offline play", link: "/reference/safety" },
              { text: "Backups and recovery", link: "/reference/restore" },
            ],
          },
          {
            text: "Releases",
            items: [
              { text: "1.6.2", link: "/releases/1.6.2" },
              { text: "1.6.1", link: "/releases/1.6.1" },
              { text: "1.6.0", link: "/releases/1.6.0" },
              { text: "1.5.39", link: "/releases/1.5.39" },
              { text: "1.5.38", link: "/releases/1.5.38" },
              { text: "1.5.37", link: "/releases/1.5.37" },
              { text: "1.5.36", link: "/releases/1.5.36" },
              { text: "1.5.35", link: "/releases/1.5.35" },
              { text: "1.5.34", link: "/releases/1.5.34" },
              { text: "1.5.33", link: "/releases/1.5.33" },
              { text: "1.5.32", link: "/releases/1.5.32" },
              { text: "1.5.31", link: "/releases/1.5.31" },
              { text: "1.5.30", link: "/releases/1.5.30" },
              { text: "1.5.29", link: "/releases/1.5.29" },
              { text: "1.5.28", link: "/releases/1.5.28" },
              { text: "1.5.27", link: "/releases/1.5.27" },
              { text: "1.5.26", link: "/releases/1.5.26" },
              { text: "1.5.25", link: "/releases/1.5.25" },
              { text: "1.5.24", link: "/releases/1.5.24" },
              { text: "1.5.23", link: "/releases/1.5.23" },
              { text: "1.5.22", link: "/releases/1.5.22" },
              { text: "1.5.21", link: "/releases/1.5.21" },
              { text: "1.5.20", link: "/releases/1.5.20" },
              { text: "1.5.19", link: "/releases/1.5.19" },
              { text: "1.5.18", link: "/releases/1.5.18" },
              { text: "1.5.17", link: "/releases/1.5.17" },
              { text: "1.5.16", link: "/releases/1.5.16" },
              { text: "1.5.15", link: "/releases/1.5.15" },
              { text: "1.5.14", link: "/releases/1.5.14" },
              { text: "1.5.13", link: "/releases/1.5.13" },
              { text: "1.5.12", link: "/releases/1.5.12" },
              { text: "1.5.11", link: "/releases/1.5.11" },
              { text: "1.5.10", link: "/releases/1.5.10" },
              { text: "1.5.9", link: "/releases/1.5.9" },
              { text: "1.5.8", link: "/releases/1.5.8" },
              { text: "1.5.7", link: "/releases/1.5.7" },
              { text: "1.5.6", link: "/releases/1.5.6" },
              { text: "1.5.5", link: "/releases/1.5.5" },
              { text: "1.5.4", link: "/releases/1.5.4" },
              { text: "1.5.3", link: "/releases/1.5.3" },
              { text: "1.5.2", link: "/releases/1.5.2" },
              { text: "1.5.1", link: "/releases/1.5.1" },
              { text: "1.5.0", link: "/releases/1.5.0" },
              { text: "1.4.0", link: "/releases/1.4.0" },
              { text: "1.3.7", link: "/releases/1.3.7" },
              { text: "1.3.6", link: "/releases/1.3.6" },
              { text: "1.3.5", link: "/releases/1.3.5" },
              { text: "1.3.4", link: "/releases/1.3.4" },
              { text: "1.3.3", link: "/releases/1.3.3" },
              { text: "1.3.2", link: "/releases/1.3.2" },
              { text: "1.3.1", link: "/releases/1.3.1" },
              { text: "1.3.0", link: "/releases/1.3.0" },
              { text: "1.2.6", link: "/releases/1.2.6" },
              { text: "1.2.5", link: "/releases/1.2.5" },
              { text: "1.2.4", link: "/releases/1.2.4" },
              { text: "1.2.3", link: "/releases/1.2.3" },
              { text: "1.2.2", link: "/releases/1.2.2" },
              { text: "1.2.1", link: "/releases/1.2.1" },
              { text: "1.2.0", link: "/releases/1.2.0" },
              { text: "1.1.0", link: "/releases/1.1.0" },
              { text: "1.0.13", link: "/releases/1.0.13" },
              { text: "1.0.12", link: "/releases/1.0.12" },
              { text: "1.0.11", link: "/releases/1.0.11" },
              { text: "1.0.10", link: "/releases/1.0.10" },
              { text: "1.0.9", link: "/releases/1.0.9" },
              { text: "1.0.8", link: "/releases/1.0.8" },
              { text: "1.0.7", link: "/releases/1.0.7" },
              { text: "1.0.6", link: "/releases/1.0.6" },
              { text: "1.0.5", link: "/releases/1.0.5" },
              { text: "1.0.4", link: "/releases/1.0.4" },
              { text: "1.0.3", link: "/releases/1.0.3" },
              { text: "1.0.2", link: "/releases/1.0.2" },
              { text: "1.0.1", link: "/releases/1.0.1" },
              { text: "1.0.0", link: "/releases/1.0.0" },
              { text: "0.2.10", link: "/releases/0.2.10" },
              { text: "0.2.9", link: "/releases/0.2.9" },
              { text: "0.2.8", link: "/releases/0.2.8" },
              { text: "0.2.7", link: "/releases/0.2.7" },
              { text: "0.2.6", link: "/releases/0.2.6" },
              { text: "0.2.5", link: "/releases/0.2.5" },
              { text: "0.2.4", link: "/releases/0.2.4" },
              { text: "0.2.3", link: "/releases/0.2.3" },
              { text: "0.2.2", link: "/releases/0.2.2" },
              { text: "0.2.1", link: "/releases/0.2.1" },
              { text: "0.2.0", link: "/releases/0.2.0" },
            ],
          },
        ],
      },
      {
        id: "developers",
        text: "Developers",
        icon: "code",
        mono: true,
        groups: [
          {
            text: "Start here",
            items: [
              { text: "What you can build", link: "/reference/", icon: "file" },
              { text: "How the pieces fit", link: "/std/overview", icon: "file" },
              { text: "What an addon is", link: "/reference/addons/", icon: "file" },
              { text: "Your first addon", link: "/reference/addons/first-addon", icon: "file" },
              { text: "Templates", link: "/reference/addons/template", icon: "file" },
            ],
          },
          {
            text: "The standard library",
            items: [
              { text: "How the pieces fit", link: "/std/overview", icon: "file" },
              {
                text: "The registry",
                link: "/std/",
                linkText: "The registry in one example",
                icon: "file",
                items: [
                  { text: "Records and schemas", link: "/std/registry", icon: "file" },
                  { text: "Keys, bases and variants", link: "/std/bases", icon: "file" },
                  { text: "How changes reach careers", link: "/std/inheritance", icon: "file" },
                  { text: "Providers", link: "/std/providers", icon: "file" },
                  { text: "Reading", link: "/std/reading", icon: "file" },
                  { text: "Layers", link: "/std/layers", icon: "file" },
                  { text: "Scopes and snapshots", link: "/std/scopes", icon: "file" },
                  { text: "When changes apply", link: "/std/build", icon: "file" },
                  { text: "Versioning", link: "/std/versioning", icon: "file" },
                ],
              },
              {
                text: "Events",
                link: "/std/events",
                linkText: "Events in full",
                icon: "file",
                items: [
                  { text: "Kinds and phases", link: "/std/events#kinds-and-phases", icon: "file" },
                  { text: "The lifecycle", link: "/std/events#the-lifecycle", icon: "file" },
                  { text: "The context", link: "/std/events#the-context", icon: "file" },
                  { text: "Scope", link: "/std/events#scope", icon: "file" },
                  { text: "Content owners", link: "/std/events#content-owners", icon: "file" },
                  { text: "When a handler faults", link: "/std/events#when-a-handler-faults", icon: "file" },
                  { text: "Your own events", link: "/std/events#your-own-events", icon: "file" },
                  { text: "Launcher events", link: "/std/events#launcher-events", icon: "file" },
                  { text: "Adding an event", link: "/std/events#adding-an-event", icon: "file" },
                ],
              },
              {
                text: "Hooks",
                link: "/std/hooks",
                linkText: "A hook in full",
                icon: "file",
                items: [
                  { text: "Phases", link: "/std/hooks#phases", icon: "file" },
                  { text: "Game hook points", link: "/std/hooks#game-hook-points", icon: "file" },
                  { text: "Hook points the game calls every frame", link: "/std/hooks#hook-points-the-game-calls-every-frame", icon: "file" },
                  { text: "Addon hook points", link: "/std/hooks#addon-hook-points", icon: "file" },
                  { text: "When a handler faults", link: "/std/hooks#when-a-handler-faults", icon: "file" },
                  { text: "Living with mixins", link: "/std/hooks#living-with-mixins", icon: "file" },
                ],
              },
              {
                text: "Services",
                link: "/std/services",
                linkText: "What a service is",
                icon: "file",
                items: [
                  { text: "Publishing", link: "/std/services#publishing", icon: "file" },
                  { text: "Looking it up", link: "/std/services#looking-it-up", icon: "file" },
                  { text: "Versions", link: "/std/services#versions", icon: "file" },
                  { text: "Replacing GetProcAddress", link: "/std/services#replacing-getprocaddress", icon: "file" },
                  { text: "Declaring", link: "/std/services#declaring", icon: "file" },
                ],
              },
              {
                text: "Operations",
                link: "/std/operations",
                linkText: "What an operation is",
                icon: "file",
                items: [
                  { text: "Applying a season", link: "/std/operations#applying-a-season", icon: "file" },
                  { text: "Setting ratings", link: "/std/operations#setting-ratings", icon: "file" },
                  { text: "The planned operations", link: "/std/operations#the-planned-operations", icon: "file" },
                  { text: "What an operation guarantees", link: "/std/operations#what-an-operation-guarantees", icon: "file" },
                ],
              },
              {
                text: "Dependencies and guards",
                link: "/std/dependencies",
                linkText: "What a dependency is",
                icon: "file",
                items: [
                  { text: "Declaring what you need", link: "/std/dependencies#declaring-what-you-need", icon: "file" },
                  { text: "Addons that break others", link: "/std/dependencies#addons-that-break-others", icon: "file" },
                  { text: "Renamed addons", link: "/std/dependencies#renamed-addons", icon: "file" },
                  { text: "Tracks have stable identities", link: "/std/dependencies#tracks-have-stable-identities", icon: "file" },
                  { text: "When a reference is missing", link: "/std/dependencies#when-a-reference-is-missing", icon: "file" },
                  { text: "Guards", link: "/std/dependencies#guards", icon: "file" },
                  { text: "Careers remember what they were made with", link: "/std/dependencies#careers-remember-what-they-were-made-with", icon: "file" },
                  { text: "Before Play", link: "/std/dependencies#before-play", icon: "file" },
                  { text: "Turning an addon off", link: "/std/dependencies#turning-an-addon-off", icon: "file" },
                  { text: "After load: the backstop", link: "/std/dependencies#after-load-the-backstop", icon: "file" },
                  { text: "The Addon Browser", link: "/std/dependencies#the-addon-browser", icon: "file" },
                ],
              },
            ],
          },
          {
            text: "Game systems",
            items: [
              { text: "Overview and limits", link: "/std/systems/", icon: "file" },
              { text: "Seasons and GP/TT content", link: "/std/content", icon: "file" },
              { text: "Cars", link: "/std/systems/cars", icon: "file" },
              { text: "Car physics and handling", link: "/std/systems/car-physics", icon: "file" },
              { text: "Liveries", link: "/std/systems/liveries", icon: "file" },
              { text: "Teams", link: "/std/systems/teams", icon: "file" },
              { text: "Drivers", link: "/std/systems/drivers", icon: "file" },
              { text: "Helmets, suits and gloves", link: "/std/systems/wearables", icon: "file" },
              { text: "Sounds", link: "/std/systems/sounds", icon: "file" },
              { text: "Tracks", link: "/std/systems/tracks", icon: "file" },
              { text: "Tyres", link: "/std/systems/tyres", icon: "file" },
              { text: "Power units, ERS and DRS", link: "/std/systems/power-units", icon: "file" },
              { text: "Sessions and weekends", link: "/std/systems/sessions", icon: "file" },
              { text: "Weather", link: "/std/systems/weather", icon: "file" },
              { text: "AI", link: "/std/systems/ai", icon: "file" },
              { text: "Career", link: "/std/systems/career", icon: "file" },
              { text: "UI and strings", link: "/std/systems/ui", icon: "file" },
              { text: "Graphics, replays and telemetry", link: "/std/systems/graphics", icon: "file" },
              { text: "Your own systems", link: "/std/systems/custom", icon: "file" },
            ],
          },
          {
            text: "Guides",
            items: [
              { text: "All guides", link: "/make/", icon: "file" },
              { text: "A what-if history editor", link: "/make/what-if", icon: "file" },
              { text: "An engine sound pack", link: "/make/engine-sounds", icon: "file" },
              { text: "A livery", link: "/make/livery", icon: "file" },
              { text: "A track", link: "/make/track", icon: "file" },
              { text: "A year pack", link: "/make/year-pack", icon: "file" },
              { text: "A career mode", link: "/make/career", icon: "file" },
              { text: "Your own system", link: "/make/own-system", icon: "file" },
              { text: "Moving a 1.5 addon to the registry", link: "/make/migrate", icon: "file" },
            ],
          },
          {
            text: "Launcher plugins",
            items: [
              { text: "Overview", link: "/reference/addons/launcher-api", icon: "file" },
              { text: "The protocol", link: "/reference/addons/launcher-api/protocol", icon: "file" },
              { text: "Host calls", link: "/reference/addons/launcher-api/host-calls", icon: "file" },
              { text: "Views", link: "/reference/addons/launcher-api/views", icon: "file" },
              { text: "Events and effects", link: "/reference/addons/launcher-api/events", icon: "file" },
              { text: "Blocks", link: "/reference/addons/launcher-api/blocks", icon: "file" },
              { text: "Forms", link: "/reference/addons/launcher-api/forms", icon: "file" },
              { text: "Tables", link: "/reference/addons/launcher-api/tables", icon: "file" },
              { text: "Live pages", link: "/reference/addons/launcher-api/live-pages", icon: "file" },
              { text: "Task lists", link: "/reference/addons/launcher-api/task-lists", icon: "file" },
              { text: "The build reply", link: "/reference/addons/launcher-api/build-reply", icon: "file" },
              { text: "Importers", link: "/reference/addons/launcher-api/importers", icon: "file" },
              { text: "Commands and summary", link: "/reference/addons/launcher-api/commands", icon: "file" },
              { text: "Registries in the launcher", link: "/std/launcher", icon: "file" },
              { text: "Changes at runtime", link: "/reference/addons/runtime-assets", icon: "file" },
            ],
          },
          {
            text: "Mods and importing",
            items: [
              { text: "The Simplified importer", link: "/std/importer", icon: "file" },
              { text: "Mods", link: "/reference/mods/", icon: "file" },
              { text: ".serps packages", link: "/reference/mods/serps", icon: "file" },
              { text: "Track packages", link: "/reference/mods/tracks", icon: "file" },
              { text: "How tracks are installed", link: "/reference/installing-tracks", icon: "file" },
            ],
          },
          {
            text: "Packaging and publishing",
            items: [
              { text: "swofty.json", link: "/reference/addons/manifest", icon: "file" },
              { text: "registry.json", link: "/std/manifest", icon: "file" },
              { text: "Packaging and publishing", link: "/reference/addons/publishing", icon: "file" },
              { text: "Testing your addon", link: "/std/testing", icon: "file" },
            ],
          },
          {
            text: "Reference",
            items: [
              { text: "Command line (swofty)", link: "/reference/cli", icon: "file" },
              { text: "The C ABI", link: "/std/abi", icon: "file" },
              { text: "Compatibility with older addons", link: "/std/compatibility", icon: "file" },
              {
                text: "Build operations",
                link: "/reference/data/",
                icon: "file",
                items: [
                  { text: "Operation files", link: "/reference/data/operations", icon: "file" },
                  { text: "Database operations", link: "/reference/ops/database", icon: "file" },
                  { text: "Game database edits", link: "/reference/data/database", icon: "file" },
                  { text: "ERP operations", link: "/reference/ops/erp", icon: "file" },
                  { text: "Textures and models", link: "/reference/data/textures", icon: "file" },
                  { text: "Liveries (build operations)", link: "/reference/data/livery", icon: "file" },
                  { text: "In-game text", link: "/reference/data/text", icon: "file" },
                  { text: "A track with its own name", link: "/reference/data/track-names", icon: "file" },
                  { text: "File operations", link: "/reference/ops/files", icon: "file" },
                  { text: "UI patches", link: "/reference/ops/ui-patches", icon: "file" },
                  { text: "Asset op files", link: "/reference/addons/asset-api", icon: "file" },
                ],
              },
              {
                text: "Game database (GVDB)",
                link: "/reference/gamedb/format",
                icon: "file",
                items: [
                  { text: "File format", link: "/reference/gamedb/format", icon: "file" },
                  { text: "How the game reads it", link: "/reference/gamedb/reading", icon: "file" },
                  { text: "Modding recipes", link: "/reference/gamedb/recipes", icon: "file" },
                  { text: "Tools", link: "/reference/gamedb/tools", icon: "file" },
                  { text: "Table catalogue", link: "/reference/gamedb/catalogue", icon: "file" },
                  { text: "Key schema", link: "/reference/gamedb/schema", icon: "file" },
                ],
              },
              { text: "ERP archives", link: "/reference/erp", icon: "file" },
              { text: "!pkg resources", link: "/reference/pkg", icon: "file" },
              { text: "Assets from older F1 games", link: "/reference/old-games", icon: "file" },
              {
                text: "In-game code (1.5 APIs)",
                link: "/reference/addons/native-plugins",
                linkText: "Engine plugins",
                icon: "file",
                items: [
                  { text: "The mixin system", link: "/reference/addons/hooking", icon: "file" },
                  { text: "Addon settings", link: "/reference/addons/settings", icon: "file" },
                  { text: "In-game UI", link: "/reference/addons/in-game-ui", icon: "file" },
                  { text: "Post-process passes", link: "/reference/addons/postfx", icon: "file" },
                  { text: "Shader packs", link: "/reference/addons/shader-packs", icon: "file" },
                  {
                    text: "Libraries",
                    link: "/reference/std/",
                    icon: "file",
                    items: [
                      { text: "Events & frame", link: "/reference/std/events", icon: "file" },
                      { text: "Overlay", link: "/reference/std/overlay", icon: "file" },
                      { text: "Telemetry", link: "/reference/std/telemetry", icon: "file" },
                      { text: "Database", link: "/reference/std/db", icon: "file" },
                      { text: "Storage", link: "/reference/std/storage", icon: "file" },
                      { text: "Career", link: "/reference/std/career", icon: "file" },
                      { text: "Popup", link: "/reference/std/popup", icon: "file" },
                      { text: "Race HUD", link: "/reference/std/hud", icon: "file" },
                      { text: "Strings", link: "/reference/std/strings", icon: "file" },
                      { text: "Career contents", link: "/reference/std/career-content", icon: "file" },
                      { text: "Characters", link: "/reference/std/characters", icon: "file" },
                      { text: "Career wizard", link: "/reference/std/career-wizard", icon: "file" },
                      { text: "Era picker boxes", link: "/reference/std/era-picker", icon: "file" },
                      { text: "Memory", link: "/reference/std/memory", icon: "file" },
                      { text: "Logging", link: "/reference/std/logging", icon: "file" },
                      { text: "Mixin API", link: "/reference/std/mixin", icon: "file" },
                      { text: "Settings", link: "/reference/std/settings", icon: "file" },
                      { text: "UI", link: "/reference/std/ui", icon: "file" },
                    ],
                  },
                  {
                    text: "Menus and UI",
                    link: "/reference/ui/",
                    icon: "file",
                    items: [
                      { text: "UI scenes (.idf)", link: "/reference/ui/idf", icon: "file" },
                      { text: "Swofty UI", link: "/reference/ui/swofty-ui", icon: "file" },
                      { text: "Editing menus", link: "/reference/ui/editing-menus", icon: "file" },
                      { text: "Techniques", link: "/reference/ui/techniques", icon: "file" },
                      { text: "Debugging live", link: "/reference/ui/debugging", icon: "file" },
                      { text: "Node reference", link: "/reference/ui/nodes", icon: "file" },
                      { text: "Patterns and checks", link: "/reference/ui/patterns", icon: "file" },
                      { text: "UI engine internals", link: "/reference/ui/engine", icon: "file" },
                    ],
                  },
                ],
              },
            ],
          },
        ],
      },
    ],
  },
});
