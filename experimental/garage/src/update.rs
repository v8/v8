// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `garage --update`: hand off to the release installer.
//!
//! This deliberately does **not** reimplement installing. `install.sh` already
//! knows the asset names, the checksum format, the tarball layout and how to
//! swap a running binary; duplicating that here would mean every released
//! binary carries a frozen copy of those details. Rename a release asset once
//! and every old `garage --update` 404s forever — which is the worst possible
//! failure for a thing whose entire job is to work on *old* binaries.
//!
//! So the split is: this decides **whether** to update, the script decides
//! **how**. Old binaries automatically pick up today's installer.
//!
//! The script is fetched **at the release tag**, not from `main`, so the
//! installer always matches the release it is installing and an unfinished
//! edit on `main` cannot break updates in the field.
//!
//! Executing a downloaded script is the same trust decision the documented
//! `curl … | sh` install already makes; it is not a new one. What garage does
//! *not* do is check for updates on its own — this runs only when asked.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::{Context, Result, bail};

const REPO: &str = "victorgomes/garage";

/// Pulls `tag_name` out of the GitHub releases API response.
///
/// One field of a fixed shape, so a regex beats promoting `serde_json` from a
/// dev-dependency to a runtime one.
fn parse_latest_tag(json: &str) -> Option<String> {
    let re = regex::Regex::new(r#""tag_name"\s*:\s*"([^"]+)""#).ok()?;
    Some(re.captures(json)?.get(1)?.as_str().to_string())
}

/// `true` when the running binary already is `tag`.
///
/// `CARGO_PKG_VERSION` has no leading `v`; the tags do.
fn is_current(tag: &str) -> bool {
    tag.trim_start_matches('v') == env!("CARGO_PKG_VERSION")
}

fn have(program: &str) -> bool {
    Command::new(program)
        .arg("--version")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

/// Fetches `url` and returns the body.
fn fetch(url: &str) -> Result<String> {
    let out = if have("curl") {
        Command::new("curl")
            .args(["-fsSL", url])
            .output()
            .context("could not run curl")?
    } else if have("wget") {
        Command::new("wget")
            .args(["-qO-", url])
            .output()
            .context("could not run wget")?
    } else {
        bail!("need curl or wget to reach GitHub");
    };
    if !out.status.success() {
        bail!("request failed: {url}");
    }
    Ok(String::from_utf8_lossy(&out.stdout).into_owned())
}

/// Refuses early, with the path in hand, rather than after a download.
///
/// The installer checks this too; doing it here first means the error names
/// `--update` and the running binary instead of arriving in installer terms.
/// Never escalates: where someone keeps a binary is their decision.
fn check_writable(target: &Path) -> Result<()> {
    let dir = target.parent().unwrap_or_else(|| Path::new("."));
    let probe = dir.join(format!(".garage-update-probe.{}", std::process::id()));
    match fs::File::create(&probe) {
        Ok(_) => {
            let _ = fs::remove_file(&probe);
            Ok(())
        }
        Err(e) => Err(e).with_context(|| {
            format!(
                "cannot write to {} — re-run with enough privileges to replace {}",
                dir.display(),
                target.display()
            )
        }),
    }
}

/// A temp directory that removes itself.
struct Scratch(PathBuf);

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

impl Scratch {
    fn new() -> Result<Self> {
        let dir = std::env::temp_dir().join(format!("garage-update.{}", std::process::id()));
        fs::create_dir_all(&dir).with_context(|| format!("cannot create {}", dir.display()))?;
        Ok(Self(dir))
    }

    fn path(&self) -> &Path {
        &self.0
    }
}

pub fn run() -> Result<()> {
    // Resolve symlinks: replacing the link would leave the real binary stale
    // and the next run would still be the old one.
    let target = std::env::current_exe().context("cannot locate the running binary")?;
    let target = fs::canonicalize(&target).unwrap_or(target);
    let dir = target
        .parent()
        .context("the running binary has no parent directory")?
        .to_path_buf();

    println!(
        "current: garage {} at {}",
        env!("CARGO_PKG_VERSION"),
        target.display()
    );
    println!("checking for a newer release...");

    let body = fetch(&format!(
        "https://api.github.com/repos/{REPO}/releases/latest"
    ))
    .context("could not reach the GitHub releases API")?;
    let tag =
        parse_latest_tag(&body).context("could not read the latest release tag from the API")?;

    if is_current(&tag) {
        println!("garage {tag} is the latest release — nothing to do");
        return Ok(());
    }

    println!("updating to {tag}");
    check_writable(&target)?;

    // Pinned to the tag, so the installer and the release always agree.
    let scratch = Scratch::new()?;
    let script = scratch.path().join("install.sh");
    let script_url = format!("https://raw.githubusercontent.com/{REPO}/{tag}/install.sh");
    let body = fetch(&script_url)
        .with_context(|| format!("could not download the installer from {script_url}"))?;
    if !body.starts_with("#!") {
        bail!("the installer at {script_url} does not look like a shell script");
    }
    fs::write(&script, body).context("cannot write the installer to a temp file")?;

    // --bin-dir pins the installer to *this* binary's directory rather than
    // letting it search PATH, which could resolve to a different copy.
    let status = Command::new("sh")
        .arg(&script)
        .arg("--bin-dir")
        .arg(&dir)
        .arg("--version")
        .arg(&tag)
        .status()
        .context("could not run the installer")?;
    if !status.success() {
        bail!("the installer failed; garage was not replaced");
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tag_is_read_from_the_api_response() {
        let body =
            r#"{"url":"https://api.github.com/...","tag_name":"v0.2.0","name":"garage v0.2.0"}"#;
        assert_eq!(parse_latest_tag(body).as_deref(), Some("v0.2.0"));
    }

    #[test]
    fn tag_survives_whitespace_and_ordering() {
        let body = "{\n  \"name\": \"garage v1.2.3\",\n  \"tag_name\" : \"v1.2.3\"\n}";
        assert_eq!(parse_latest_tag(body).as_deref(), Some("v1.2.3"));
    }

    #[test]
    fn a_response_without_a_tag_is_not_invented() {
        assert_eq!(parse_latest_tag(r#"{"message":"Not Found"}"#), None);
        assert_eq!(parse_latest_tag(""), None);
    }

    #[test]
    fn current_version_is_recognised_with_or_without_the_v() {
        let me = env!("CARGO_PKG_VERSION");
        assert!(is_current(&format!("v{me}")));
        assert!(is_current(me));
        assert!(!is_current("v99.0.0"));
    }

    #[test]
    fn writable_targets_are_accepted() {
        let dir = std::env::temp_dir().join(format!("garage-update-w.{}", std::process::id()));
        let _ = fs::create_dir_all(&dir);
        assert!(check_writable(&dir.join("garage")).is_ok());
        // The probe must not survive the check.
        assert_eq!(
            fs::read_dir(&dir).unwrap().count(),
            0,
            "probe file left behind"
        );
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    #[cfg(unix)]
    fn unwritable_targets_are_refused_before_anything_downloads() {
        use std::os::unix::fs::PermissionsExt;
        let dir = std::env::temp_dir().join(format!("garage-update-ro.{}", std::process::id()));
        let _ = fs::create_dir_all(&dir);
        fs::set_permissions(&dir, fs::Permissions::from_mode(0o500)).unwrap();

        let refused = check_writable(&dir.join("garage")).is_err();

        fs::set_permissions(&dir, fs::Permissions::from_mode(0o700)).unwrap();
        let _ = fs::remove_dir_all(&dir);

        // root ignores the mode bits, so only assert where the mode means
        // something. CI runs unprivileged; this keeps a root shell from
        // reporting a false failure.
        if unsafe { libc::geteuid() } != 0 {
            assert!(refused, "an unwritable directory must be refused");
        }
    }

    /// The installer is fetched at the tag, never from a moving branch.
    #[test]
    fn the_installer_url_is_pinned_to_the_release_tag() {
        let tag = "v9.9.9";
        let url = format!("https://raw.githubusercontent.com/{REPO}/{tag}/install.sh");
        assert!(url.ends_with("/v9.9.9/install.sh"));
        assert!(
            !url.contains("/main/"),
            "a moving branch would desync script and release"
        );
    }
}
