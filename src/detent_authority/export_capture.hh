#pragma once
#include "export_scope.hh"
#include "state.hh"

namespace detent::authority {
// Authority-owner projection: no producer sessions, receipts or private
// checkpoint state. Keep capture and cursor ownership on that executor.
class ExportCapture {
	using Records = detail::PersistentMap<std::string, Record>;
	using Namespaces = detail::PersistentMap<std::string, Revision>;
	struct FactoryTag {
		explicit FactoryTag() = default;
	};
	friend class State;

  public:
	ExportCapture(FactoryTag, const State &state, std::shared_ptr<const AuthorizedScope> scope);
	ExportCapture(const ExportCapture &) = delete;
	const distribution::Cursor &cursor() const noexcept { return cursor_; }
	const AuthorizedScope &scope() const noexcept { return *scope_; }
	// The owner drops all cursors before retirement, and retains this capture
	// and its slot until reclamation_pending() becomes false.
	void retire() noexcept;
	std::size_t collect(CommandWorkBudget &budget) noexcept;
	bool reclamation_pending() const noexcept;
	Revision namespace_revision(std::uint64_t ordinal) const;
	class RecordCursor {
		friend class ExportCapture;
		RecordCursor(const Records &records, std::shared_ptr<const AuthorizedScope> scope,
					 std::shared_ptr<detail::DeferredValues<KnobDef>> definitions, std::uint32_t index);
		std::shared_ptr<const AuthorizedScope> scope_;
		std::shared_ptr<detail::DeferredValues<KnobDef>> definitions_;
		Records::Cursor cursor_;
		std::string_view name_;
		std::uint32_t index_;
		bool ended_ = false;
		static Records::Cursor start(const Records &records, std::string_view name);

	  public:
		struct Row {
			std::uint32_t namespace_index;
			std::string_view key;
			Revision revision;
			const KnobDef *definition;
		};
		std::optional<Row> next();
	};
	RecordCursor records(std::uint64_t namespace_ordinal) const;
	std::shared_ptr<distribution::StorageLedger> storage_account() const noexcept {
		return scope_->storage_account();
	}

  private:
	std::shared_ptr<const AuthorizedScope> scope_;
	distribution::Cursor cursor_;
	Records records_;
	Namespaces namespaces_;
	std::shared_ptr<detail::DeferredValues<KnobDef>> definitions_;
	unsigned collection_cursor_ = 0;
	bool retired_ = false;
};
} // namespace detent::authority
