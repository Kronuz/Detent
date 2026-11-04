//! T4 (docs/design/client-local-source.md): opening, looking up and dropping a Reader leaves no
//! descriptor behind. One test per process, so no other test's descriptors move the count.
mod common;

use common::{descriptors, fresh, knobs, uid, Writer};
use detent::Reader;

#[test]
fn open_lookup_drop_leaks_no_descriptor() {
    let store = fresh("lifetime");
    let writer = Writer::open(&store);
    writer.publish(&knobs(1));
    // One warm-up cycle so lazily created process state is not counted.
    drop(Reader::open(&store, uid(), &[]).unwrap());
    let before = descriptors();
    for _ in 0..20 {
        let reader = Reader::open(&store, uid(), &[]).unwrap();
        assert_eq!(reader.get("t:rev", -1), 1);
        assert_eq!(reader.revision("t"), Some(1));
    }
    assert_eq!(descriptors(), before);
}
