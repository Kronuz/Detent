#pragma once
#include "detent_local/paged_layout.hh"
namespace detent::local::detail {
inline bool reuses_paged_state(const paged::Root &base, const paged::Root &target) noexcept {
	return base.cursor.same_identity(target.cursor) && target.cursor.revision >= base.cursor.revision &&
		   base.profile == target.profile && base.tree == target.tree && base.logical == target.logical &&
		   base.bytes == target.bytes && base.pages == target.pages && base.segments == target.segments &&
		   base.records == target.records && base.namespaces == target.namespaces;
}
} // namespace detent::local::detail
