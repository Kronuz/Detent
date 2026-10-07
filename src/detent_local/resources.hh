#pragma once
#include "detent_local/view.hh"
namespace detent::local::resources {
distribution::RetirementDomain &domain(const ViewLimits &limits);
std::shared_ptr<distribution::StorageLedger> mapping_account(const ViewLimits &limits);
} // namespace detent::local::resources
