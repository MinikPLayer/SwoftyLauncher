use std::ffi::c_void;
use std::os::windows::ffi::OsStrExt;
use std::path::Path;

const TH32CS_SNAPPROCESS: u32 = 0x2;
const INVALID_HANDLE_VALUE: *mut c_void = -1isize as *mut c_void;
const MAX_PATH: usize = 260;
const PROCESS_QUERY_LIMITED_INFORMATION: u32 = 0x1000;

#[repr(C)]
struct ProcessEntry32W {
    size: u32,
    usage: u32,
    process_id: u32,
    default_heap_id: usize,
    module_id: u32,
    threads: u32,
    parent_process_id: u32,
    priority: i32,
    flags: u32,
    exe_file: [u16; MAX_PATH],
}

#[link(name = "kernel32")]
unsafe extern "system" {
    fn CreateToolhelp32Snapshot(flags: u32, pid: u32) -> *mut c_void;
    fn Process32FirstW(snapshot: *mut c_void, entry: *mut ProcessEntry32W) -> i32;
    fn Process32NextW(snapshot: *mut c_void, entry: *mut ProcessEntry32W) -> i32;
    fn OpenProcess(access: u32, inherit: i32, pid: u32) -> *mut c_void;
    fn QueryFullProcessImageNameW(process: *mut c_void, flags: u32, name: *mut u16, size: *mut u32) -> i32;
    fn CloseHandle(handle: *mut c_void) -> i32;
}

pub fn processes() -> Vec<(u32, String)> {
    // SAFETY: the snapshot handle is checked before use and closed once;
    // the entry is zeroed with its size set, as Process32FirstW requires,
    // and exe_file is read only up to its NUL inside the array.
    unsafe {
        let snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if snapshot == INVALID_HANDLE_VALUE || snapshot.is_null() {
            return Vec::new();
        }
        let mut entry: ProcessEntry32W = std::mem::zeroed();
        entry.size = std::mem::size_of::<ProcessEntry32W>() as u32;
        let mut out = Vec::new();
        let mut more = Process32FirstW(snapshot, &mut entry) != 0;
        while more {
            let len = entry.exe_file.iter().position(|&c| c == 0).unwrap_or(MAX_PATH);
            out.push((entry.process_id, String::from_utf16_lossy(&entry.exe_file[..len])));
            more = Process32NextW(snapshot, &mut entry) != 0;
        }
        CloseHandle(snapshot);
        out
    }
}

pub fn image(pid: u32) -> Option<String> {
    let mut name = vec![0u16; 32_768];
    let mut len = name.len() as u32;
    // SAFETY: the handle is checked before use and closed once, and `len`
    // tells the call how many u16s `name` holds.
    let ok = unsafe {
        let process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid);
        if process.is_null() {
            return None;
        }
        let ok = QueryFullProcessImageNameW(process, 0, name.as_mut_ptr(), &mut len);
        CloseHandle(process);
        ok
    };
    (ok != 0).then(|| String::from_utf16_lossy(&name[..len as usize]))
}

/// The full image paths of the running processes whose exe file is `name`.
pub fn images(name: &str) -> Vec<String> {
    processes()
        .into_iter()
        .filter(|(_, file)| file.eq_ignore_ascii_case(name))
        .filter_map(|(pid, _)| image(pid))
        .collect()
}

pub fn junction(target: &Path, at: &Path) -> std::io::Result<()> {
    const GENERIC_WRITE: u32 = 0x4000_0000;
    const OPEN_EXISTING: u32 = 3;
    const FILE_FLAG_BACKUP_SEMANTICS: u32 = 0x0200_0000;
    const FILE_FLAG_OPEN_REPARSE_POINT: u32 = 0x0020_0000;
    const FSCTL_SET_REPARSE_POINT: u32 = 0x0009_00A4;
    const IO_REPARSE_TAG_MOUNT_POINT: u32 = 0xA000_0003;
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn CreateFileW(
            name: *const u16,
            access: u32,
            share: u32,
            security: *const c_void,
            disposition: u32,
            flags: u32,
            template: *mut c_void,
        ) -> *mut c_void;
        fn DeviceIoControl(
            device: *mut c_void,
            code: u32,
            input: *const c_void,
            input_size: u32,
            output: *mut c_void,
            output_size: u32,
            returned: *mut u32,
            overlapped: *mut c_void,
        ) -> i32;
    }
    let target = std::path::absolute(target)?;
    let plain = target.to_string_lossy();
    let plain = plain.strip_prefix(r"\\?\").unwrap_or(&plain);
    let print: Vec<u16> = plain.encode_utf16().collect();
    let substitute: Vec<u16> = r"\??\".encode_utf16().chain(print.iter().copied()).collect();
    let bytes =
        |s: &[u16]| u16::try_from(s.len() * 2).map_err(|_| std::io::Error::other("the path is too long"));
    let (sub, prn) = (bytes(&substitute)?, bytes(&print)?);
    let mut data: Vec<u8> = Vec::new();
    data.extend(IO_REPARSE_TAG_MOUNT_POINT.to_le_bytes());
    data.extend((8 + sub + 2 + prn + 2).to_le_bytes());
    data.extend([0, 0, 0, 0]);
    data.extend(sub.to_le_bytes());
    data.extend((sub + 2).to_le_bytes());
    data.extend(prn.to_le_bytes());
    for unit in substitute.iter().chain(&[0]).chain(&print).chain(&[0]) {
        data.extend(unit.to_le_bytes());
    }
    std::fs::create_dir(at)?;
    let wide: Vec<u16> = at.as_os_str().encode_wide().chain([0]).collect();
    // SAFETY: `wide` is NUL-terminated, the handle is checked before use and
    // closed once, and DeviceIoControl reads `data.len()` bytes of `data`.
    let set = unsafe {
        let dir = CreateFileW(
            wide.as_ptr(),
            GENERIC_WRITE,
            0,
            std::ptr::null(),
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            std::ptr::null_mut(),
        );
        if dir == INVALID_HANDLE_VALUE {
            Err(std::io::Error::last_os_error())
        } else {
            let mut returned = 0;
            let ok = DeviceIoControl(
                dir,
                FSCTL_SET_REPARSE_POINT,
                data.as_ptr().cast(),
                data.len() as u32,
                std::ptr::null_mut(),
                0,
                &mut returned,
                std::ptr::null_mut(),
            );
            let set = if ok != 0 {
                Ok(())
            } else {
                Err(std::io::Error::last_os_error())
            };
            CloseHandle(dir);
            set
        }
    };
    if set.is_err() {
        std::fs::remove_dir(at).ok();
    }
    set
}

pub fn set_user_env(name: &str, value: &Path) -> std::io::Result<()> {
    const HKEY_CURRENT_USER: isize = 0x8000_0001_u32 as i32 as isize;
    const REG_SZ: u32 = 1;
    const HWND_BROADCAST: isize = 0xffff;
    const WM_SETTINGCHANGE: u32 = 0x001A;
    const SMTO_ABORTIFHUNG: u32 = 0x0002;
    #[link(name = "advapi32")]
    unsafe extern "system" {
        fn RegSetKeyValueW(
            key: isize,
            sub_key: *const u16,
            name: *const u16,
            kind: u32,
            data: *const c_void,
            size: u32,
        ) -> i32;
    }
    #[link(name = "user32")]
    unsafe extern "system" {
        fn SendMessageTimeoutW(
            window: isize,
            msg: u32,
            wparam: usize,
            lparam: isize,
            flags: u32,
            timeout: u32,
            result: *mut usize,
        ) -> isize;
    }
    let wide = |s: &std::ffi::OsStr| -> Vec<u16> { s.encode_wide().chain([0]).collect() };
    let environment = wide("Environment".as_ref());
    let name = wide(name.as_ref());
    let data = wide(value.as_os_str());
    // SAFETY: every string is NUL-terminated and outlives the calls, and the
    // value's size counts its u16s and the NUL in bytes.
    unsafe {
        let status = RegSetKeyValueW(
            HKEY_CURRENT_USER,
            environment.as_ptr(),
            name.as_ptr(),
            REG_SZ,
            data.as_ptr().cast(),
            (data.len() * 2) as u32,
        );
        if status != 0 {
            return Err(std::io::Error::from_raw_os_error(status));
        }
        let mut result = 0;
        SendMessageTimeoutW(
            HWND_BROADCAST,
            WM_SETTINGCHANGE,
            0,
            environment.as_ptr() as isize,
            SMTO_ABORTIFHUNG,
            5000,
            &mut result,
        );
    }
    Ok(())
}

/// Starts `exe` in `dir` through the desktop's shell, which runs it as the
/// signed-in user, unelevated and with the shell's own environment, whatever
/// this process runs as: `IShellDispatch2::ShellExecute` on the desktop
/// window's folder view.
pub fn shell_open(exe: &Path, dir: &Path) -> windows::core::Result<()> {
    use std::mem::ManuallyDrop;
    use windows::Win32::System::Com::{
        CLSCTX_LOCAL_SERVER, COINIT_APARTMENTTHREADED, CoCreateInstance, CoInitializeEx, CoUninitialize,
        IDispatch, IServiceProvider,
    };
    use windows::Win32::System::Variant::{
        VARENUM, VARIANT, VARIANT_0, VARIANT_0_0, VARIANT_0_0_0, VT_BSTR, VT_I4, VariantClear,
    };
    use windows::Win32::UI::Shell::{
        CSIDL_DESKTOP, IShellBrowser, IShellDispatch2, IShellFolderViewDual, IShellWindows,
        SID_STopLevelBrowser, SVGIO_BACKGROUND, SWC_DESKTOP, SWFO_NEEDDISPATCH, ShellWindows,
    };
    use windows::core::{BSTR, Interface};
    const SW_SHOWNORMAL: i32 = 1;

    fn bstr(path: &Path) -> BSTR {
        BSTR::from_wide(&path.as_os_str().encode_wide().collect::<Vec<_>>())
    }
    fn variant(vt: VARENUM, value: VARIANT_0_0_0) -> VARIANT {
        let value = VARIANT_0_0 {
            vt,
            Anonymous: value,
            ..Default::default()
        };
        VARIANT {
            Anonymous: VARIANT_0 {
                Anonymous: ManuallyDrop::new(value),
            },
        }
    }
    let int = |n| variant(VT_I4, VARIANT_0_0_0 { lVal: n });

    // SAFETY: COM stays initialised on this thread until every interface has
    // been dropped, and `folder`, the one VARIANT that owns a string, is
    // cleared once.
    unsafe {
        let init = CoInitializeEx(None, COINIT_APARTMENTTHREADED);
        let done = (|| {
            let windows: IShellWindows = CoCreateInstance(&ShellWindows, None, CLSCTX_LOCAL_SERVER)?;
            let mut hwnd = 0;
            let desktop = windows.FindWindowSW(
                &int(CSIDL_DESKTOP as i32),
                &VARIANT::default(),
                SWC_DESKTOP,
                &mut hwnd,
                SWFO_NEEDDISPATCH,
            )?;
            let browser: IShellBrowser = desktop
                .cast::<IServiceProvider>()?
                .QueryService(&SID_STopLevelBrowser)?;
            let view: IDispatch = browser.QueryActiveShellView()?.GetItemObject(SVGIO_BACKGROUND)?;
            let shell: IShellDispatch2 = view.cast::<IShellFolderViewDual>()?.Application()?.cast()?;
            let mut folder = variant(
                VT_BSTR,
                VARIANT_0_0_0 {
                    bstrVal: ManuallyDrop::new(bstr(dir)),
                },
            );
            let none = VARIANT::default();
            let done = shell.ShellExecute(&bstr(exe), &none, &folder, &none, &int(SW_SHOWNORMAL));
            VariantClear(&mut folder).ok();
            done
        })();
        if init.is_ok() {
            CoUninitialize();
        }
        done
    }
}

/// The application manifest embedded in this program.
#[cfg(test)]
pub fn manifest() -> Option<String> {
    const RT_MANIFEST: usize = 24;
    const ID: usize = 1;
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn FindResourceW(module: *mut c_void, name: *const u16, kind: *const u16) -> *mut c_void;
        fn LoadResource(module: *mut c_void, info: *mut c_void) -> *mut c_void;
        fn LockResource(data: *mut c_void) -> *const u8;
        fn SizeofResource(module: *mut c_void, info: *mut c_void) -> u32;
    }
    // SAFETY: a null module is this program's own image, the integer name and
    // type are MAKEINTRESOURCE values, and the locked bytes live as long as
    // the image and are read only up to the resource's size.
    unsafe {
        let module = std::ptr::null_mut();
        let info = FindResourceW(module, ID as *const u16, RT_MANIFEST as *const u16);
        if info.is_null() {
            return None;
        }
        let data = LockResource(LoadResource(module, info));
        if data.is_null() {
            return None;
        }
        let bytes = std::slice::from_raw_parts(data, SizeofResource(module, info) as usize);
        Some(String::from_utf8_lossy(bytes).into_owned())
    }
}
