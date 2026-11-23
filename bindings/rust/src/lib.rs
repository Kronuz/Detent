//! Rust binding over libdetent_reader's C ABI (docs/design/client-local-source.md D1-D3).
//!
//! It reimplements nothing: every lookup is one call into the reader core, which returns the caller's
//! fallback on any error. Only [`Reader::open`] fails loudly. The handle closes when the [`Reader`] drops,
//! and lookups borrow `&self`, so a close can never overlap one.
use std::ffi::{c_char, c_int, CStr};
use std::fmt;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;

#[repr(C)]
#[derive(Clone, Copy)]
struct Str {
    data: *const c_char,
    size: usize,
}

#[repr(C)]
struct OpenOptions {
    size: usize,
    store: Str,
    namespaces: *const Str,
    namespace_count: usize,
    publisher_uid: u32,
    mapping_bytes: u64,
}

#[repr(C)]
struct RawReader {
    _private: [u8; 0],
}

// libdetent_reader is linked by build.rs. Statuses are read as c_int: the C ABI may add values without a
// version bump, and a Rust enum holding an unknown discriminant would be undefined behavior.
extern "C" {
    fn detent_abi_version() -> u32;
    fn detent_open(options: *const OpenOptions, out: *mut *mut RawReader) -> c_int;
    fn detent_get(
        reader: *mut RawReader,
        name: Str,
        hash: Str,
        switch_value: Str,
        fallback: i64,
        out: *mut i64,
    ) -> c_int;
    fn detent_refresh(reader: *mut RawReader) -> c_int;
    fn detent_revision(reader: *mut RawReader, name_space: Str, out: *mut u64) -> c_int;
    fn detent_status_name(status: c_int) -> *const c_char;
    fn detent_close(reader: *mut RawReader);
}

const ABI_VERSION: u32 = 1;

/// The smallest `mapping_bytes` [`Reader::open`] accepts: 1 MiB of pack and its 6,560-byte bitmap.
pub const MINIMUM_MAPPING_BYTES: u64 = 1_055_136;

/// Why a lookup or an open did not succeed.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum Status {
    Missing,
    InvalidName,
    /// The namespace is outside the handle's list.
    NotOpened,
    /// The generation failed verification.
    SourceFailure,
    /// No prepared generation for this namespace yet.
    Unavailable,
    /// Ownership, mode, ACL or ancestry refused.
    Untrusted,
    InvalidArgument,
    NoMemory,
    Io,
    /// A status newer than this binding knows.
    Unknown(u32),
}

impl Status {
    fn from_raw(raw: c_int) -> Status {
        match raw {
            1 => Status::Missing,
            2 => Status::InvalidName,
            3 => Status::NotOpened,
            4 => Status::SourceFailure,
            5 => Status::Unavailable,
            6 => Status::Untrusted,
            7 => Status::InvalidArgument,
            8 => Status::NoMemory,
            9 => Status::Io,
            other => Status::Unknown(other as u32),
        }
    }

    /// The core's own name for the status (`"missing"`, `"untrusted"`, ...).
    pub fn name(&self) -> String {
        let raw: c_int = match self {
            Status::Missing => 1,
            Status::InvalidName => 2,
            Status::NotOpened => 3,
            Status::SourceFailure => 4,
            Status::Unavailable => 5,
            Status::Untrusted => 6,
            Status::InvalidArgument => 7,
            Status::NoMemory => 8,
            Status::Io => 9,
            Status::Unknown(other) => *other as c_int,
        };
        // SAFETY: detent_status_name returns a static NUL-terminated string for any value.
        unsafe { CStr::from_ptr(detent_status_name(raw)) }.to_string_lossy().into_owned()
    }
}

mod sealed {
    pub trait Sealed {}
}

/// A type [`Reader::get_as`] reads a knob as: `bool` (0 or 1) or a fixed-width integer.
pub trait Knob: Copy + sealed::Sealed {
    #[doc(hidden)]
    fn to_knob(self) -> i64;
    #[doc(hidden)]
    fn from_knob(value: i64) -> Option<Self>;
}

impl sealed::Sealed for bool {}
impl Knob for bool {
    fn to_knob(self) -> i64 {
        self as i64
    }
    fn from_knob(value: i64) -> Option<Self> {
        match value {
            0 => Some(false),
            1 => Some(true),
            _ => None,
        }
    }
}

macro_rules! integer_knob {
    ($($t:ty),*) => {$(
        impl sealed::Sealed for $t {}
        impl Knob for $t {
            // A fallback outside i64 wraps here and comes back out of range, so get_as returns it as is.
            fn to_knob(self) -> i64 {
                self as i64
            }
            fn from_knob(value: i64) -> Option<Self> {
                <$t>::try_from(value).ok()
            }
        }
    )*};
}
integer_knob!(i8, i16, i32, i64, u8, u16, u32, u64);

/// Opening failed; the only error that is ever raised.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReaderError {
    pub status: Status,
}

impl fmt::Display for ReaderError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "detent_open: {}", self.status.name())
    }
}

impl std::error::Error for ReaderError {}

/// A lock-free reader over an agent's store. One handle per process, shared through `Arc`.
pub struct Reader {
    handle: *mut RawReader,
}

// SAFETY: lookups are thread-safe on one handle (reader.h), and a close cannot race them because
// lookups borrow `&self` and `Drop` needs exclusive ownership.
unsafe impl Send for Reader {}
unsafe impl Sync for Reader {}

fn text(s: &str) -> Str {
    Str { data: s.as_ptr() as *const c_char, size: s.len() }
}

fn optional(s: Option<&str>) -> Str {
    match s {
        Some(s) => text(s),
        None => Str { data: std::ptr::null(), size: 0 },
    }
}

impl Reader {
    /// Opens `store`. `publisher_uid` is the uid the agent runs as: trust is never inferred from the
    /// directory's owner. `namespaces` limits the handle; empty means all, including later ones. A store
    /// that does not exist yet opens, and lookups return the fallback with [`Status::Unavailable`].
    /// `mapping_bytes` caps mapped packs and their bitmaps; 0 means 512 MiB, and a cap below one pack
    /// and its bitmap ([`MINIMUM_MAPPING_BYTES`]) fails with [`Status::InvalidArgument`].
    pub fn open(
        store: impl AsRef<Path>,
        publisher_uid: u32,
        namespaces: &[&str],
        mapping_bytes: u64,
    ) -> Result<Reader, ReaderError> {
        // SAFETY: no pointers are passed.
        if unsafe { detent_abi_version() } != ABI_VERSION {
            return Err(ReaderError { status: Status::InvalidArgument });
        }
        let path = store.as_ref().as_os_str().as_bytes();
        let names: Vec<Str> = namespaces.iter().map(|n| text(n)).collect();
        let options = OpenOptions {
            size: std::mem::size_of::<OpenOptions>(),
            store: Str { data: path.as_ptr() as *const c_char, size: path.len() },
            namespaces: names.as_ptr(),
            namespace_count: names.len(),
            publisher_uid,
            mapping_bytes,
        };
        let mut handle: *mut RawReader = std::ptr::null_mut();
        // SAFETY: `options` and what it points to outlive the call; detent_open copies what it keeps.
        let raw = unsafe { detent_open(&options, &mut handle) };
        if raw != 0 {
            return Err(ReaderError { status: Status::from_raw(raw) });
        }
        Ok(Reader { handle })
    }

    /// The knob's value, or `fallback` on any error.
    pub fn get(
        &self,
        name: &str,
        fallback: i64,
        hash: Option<&str>,
        switch_value: Option<&str>,
    ) -> i64 {
        self.lookup(name, fallback, hash, switch_value).unwrap_or(fallback)
    }

    /// The knob read as `T`, as C++'s `get_as<T>`: `fallback` on any error or when the value is out of
    /// `T`'s range (for `bool`, other than 0 or 1).
    pub fn get_as<T: Knob>(
        &self,
        name: &str,
        fallback: T,
        hash: Option<&str>,
        switch_value: Option<&str>,
    ) -> T {
        T::from_knob(self.get(name, fallback.to_knob(), hash, switch_value)).unwrap_or(fallback)
    }

    /// The knob's value, or the status that explains why it is unavailable.
    pub fn lookup(
        &self,
        name: &str,
        fallback: i64,
        hash: Option<&str>,
        switch_value: Option<&str>,
    ) -> Result<i64, Status> {
        let mut out = fallback;
        // SAFETY: the handle is live for `&self`; every string outlives the call.
        let raw = unsafe {
            detent_get(self.handle, text(name), optional(hash), optional(switch_value), fallback, &mut out)
        };
        match raw {
            0 => Ok(out),
            raw => Err(Status::from_raw(raw)),
        }
    }

    /// Attaches now rather than on the next lookup; call it from a thread of your choosing.
    pub fn refresh(&self) -> Result<(), Status> {
        // SAFETY: the handle is live for `&self`.
        match unsafe { detent_refresh(self.handle) } {
            0 => Ok(()),
            raw => Err(Status::from_raw(raw)),
        }
    }

    /// The revision of `namespace`'s attached generation, or `None` when it is not available.
    pub fn revision(&self, namespace: &str) -> Option<u64> {
        let mut out = 0u64;
        // SAFETY: the handle is live for `&self`; `namespace` outlives the call.
        match unsafe { detent_revision(self.handle, text(namespace), &mut out) } {
            0 => Some(out),
            _ => None,
        }
    }
}

impl Drop for Reader {
    fn drop(&mut self) {
        // SAFETY: `&mut self` means no lookup is in flight; the handle is closed exactly once.
        unsafe { detent_close(self.handle) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Statuses are numbered by hand; every value through the last known must name what reader.h names.
    #[test]
    fn statuses_match_the_header() {
        let names = [
            "ok", "missing", "invalid_name", "not_opened", "source_failure", "unavailable", "untrusted",
            "invalid_argument", "no_memory", "io",
        ];
        for (raw, name) in names.iter().enumerate().skip(1) {
            // SAFETY: detent_status_name returns a static NUL-terminated string for any value.
            let core = unsafe { CStr::from_ptr(detent_status_name(raw as c_int)) }.to_string_lossy().into_owned();
            assert_eq!(&core, name, "reader.h names status {raw}");
            assert_eq!(Status::from_raw(raw as c_int).name(), *name, "Status::from_raw({raw})");
        }
        assert_eq!(Status::Untrusted.name(), "untrusted");
        assert_eq!(Status::Unavailable.name(), "unavailable");
        assert_eq!(Status::Io.name(), "io");
    }

    #[test]
    fn knob_conversions() {
        assert_eq!(bool::from_knob(2), None);
        assert_eq!(i8::from_knob(128), None);
        assert_eq!(u64::from_knob(-1), None);
        assert_eq!(u64::from_knob(7), Some(7));
    }
}
