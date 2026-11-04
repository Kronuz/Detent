//! Shared by the integration tests: the test generation writer (libdetent_testing, declared here so it
//! never reaches the library) and stores under cargo's target directory.
#![allow(dead_code)]
use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};

#[link(name = "detent_testing")]
extern "C" {
    fn detent_testing_open(store: *const c_char) -> *mut c_void;
    fn detent_testing_open_error() -> *const c_char;
    fn detent_testing_publish(
        writer: *mut c_void,
        json: *const c_char,
        length: usize,
        revision: *mut u64,
    ) -> c_int;
    fn detent_testing_error(writer: *const c_void) -> *const c_char;
    fn detent_testing_close(writer: *mut c_void);
}

/// Publishes revision N+1 of a store into its directory.
pub struct Writer(*mut c_void);

unsafe impl Send for Writer {}

impl Writer {
    pub fn open(store: &Path) -> Writer {
        let path = CString::new(store.as_os_str().as_bytes()).unwrap();
        let handle = unsafe { detent_testing_open(path.as_ptr()) };
        assert!(!handle.is_null(), "{}", unsafe {
            CStr::from_ptr(detent_testing_open_error()).to_string_lossy()
        });
        Writer(handle)
    }

    pub fn publish(&self, json: &str) -> u64 {
        let mut revision = 0u64;
        let status =
            unsafe { detent_testing_publish(self.0, json.as_ptr() as *const c_char, json.len(), &mut revision) };
        assert_eq!(status, 0, "{}", unsafe { CStr::from_ptr(detent_testing_error(self.0)).to_string_lossy() });
        revision
    }
}

impl Drop for Writer {
    fn drop(&mut self) {
        unsafe { detent_testing_close(self.0) };
    }
}

/// A fresh store path under cargo's target directory, whose ancestors are not group- or world-writable.
pub fn fresh(name: &str) -> PathBuf {
    let path = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join(format!("{name}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&path);
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    path
}

pub fn knobs(rev: i64) -> String {
    format!(r#"{{"schema_version":1,"knobs":{{"t:rev":{{"type":"int","default":{rev}}}}}}}"#)
}

pub fn descriptors() -> usize {
    std::fs::read_dir("/dev/fd").unwrap().count()
}

pub fn uid() -> u32 {
    extern "C" {
        fn getuid() -> u32;
    }
    unsafe { getuid() }
}
