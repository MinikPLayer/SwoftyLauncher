use std::path::PathBuf;

pub fn data_dir() -> PathBuf {
    if let Some(dir) = std::env::var_os("SWOFTY_DATA_DIR") {
        return PathBuf::from(dir);
    }
    platform_data_dir()
}

#[cfg(windows)]
pub fn platform_data_dir() -> PathBuf {
    std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."))
        .join("Swofty")
}

#[cfg(not(windows))]
pub fn platform_data_dir() -> PathBuf {
    let home = std::env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."));
    let flatpak = home.join(".var/app/com.valvesoftware.Steam/data");
    if flatpak.is_dir() && !home.join(".local/share/Steam").exists() {
        return flatpak.join("swofty");
    }
    std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| home.join(".local/share"))
        .join("swofty")
}

pub fn game_folder_view(path: &std::path::Path) -> String {
    if cfg!(windows) {
        return path.display().to_string();
    }
    format!("Z:{}", path.display().to_string().replace('/', "\\"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn proton_view_uses_z_drive() {
        if cfg!(windows) {
            return;
        }
        assert_eq!(
            game_folder_view(std::path::Path::new("/home/a/b")),
            "Z:\\home\\a\\b"
        );
    }
}
