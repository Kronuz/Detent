#pragma once
#include "generation.hh"

namespace detent::local {
// Constructed from trusted host configuration and the actual selection.
// Decoded candidate bytes cannot grant permission to change authority identity.
struct TrustedIdentityReset {
	Descriptor expected_selected;
	distribution::Cursor configured; // Identity fields; revision is ignored.
	distribution::Profile profile;
};
} // namespace detent::local
