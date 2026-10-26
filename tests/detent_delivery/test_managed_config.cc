#include "../../src/detent_agent/managed_config.hh"
#include "detent/scope.hh"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <unistd.h>

void check(bool value) {
	if (!value)
		throw std::runtime_error("managed configuration invariant");
}
#ifdef __APPLE__
#include <sys/stat.h>
#include <sys/wait.h>

void chmod_acl(const std::filesystem::path &path, const char *operation, const char *rule = nullptr) {
	const auto child = ::fork();
	check(child >= 0);
	if (child == 0) {
		if (rule)
			::execlp("chmod", "chmod", operation, rule, path.c_str(), nullptr);
		else
			::execlp("chmod", "chmod", operation, path.c_str(), nullptr);
		::_exit(127);
	}
	int status = 0;
	check(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
#endif
int main(int argc, char **argv) {
	try {
		check(argc == 2);
		const auto directory =
			std::filesystem::current_path() / ".scratch" / ("managed-config-" + std::to_string(::getpid()));
		std::filesystem::create_directories(directory);
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
		const auto configuration = directory / "managed.json";
		rapidjson::Document object;
		object.Parse(R"({"cluster":"63636363636363636363636363636363",
"group":"67676767676767676767676767676767","epoch":"65656565656565656565656565656565",
"principal":"reader","namespaces":["z","app","app"],"token":"0123456789abcdef0123456789abcdef"})");
		auto ca = (std::filesystem::path(argv[1]) / "ca.pem").string();
		object.AddMember("ca", rapidjson::Value(ca.data(), ca.size(), object.GetAllocator()),
						 object.GetAllocator());
		auto write = [&] {
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
			object.Accept(writer);
			std::ofstream output(configuration, std::ios::binary | std::ios::trunc);
			output.write(buffer.GetString(), buffer.GetSize());
			output.close();
			check(bool(output));
			std::filesystem::permissions(configuration, std::filesystem::perms::owner_read |
															std::filesystem::perms::owner_write);
		};
		write();
		auto load = [&] {
			return detent_agent::load_managed_options(configuration, directory / "state",
													  detent::local::StoreOpenMode::ExistingManaged, false);
		};
		auto options = load();
		check(options.coordinator.source.namespaces == std::vector<std::string>({"app", "z"}));
		const std::array<std::string_view, 2> names{"app", "z"};
		check(options.coordinator.source.configured.scope ==
			  detent::distribution::canonical_scope_identity("reader", 1, names));
		check(options.disk.mode == detent::local::StoreOpenMode::ExistingManaged &&
			  options.disk.artifact_mode == 0600);
		auto fresh = detent_agent::load_managed_options(configuration, directory / "state",
														detent::local::StoreOpenMode::FreshEmpty, true);
		check(fresh.disk.mode == detent::local::StoreOpenMode::FreshEmpty &&
			  fresh.disk.artifact_mode == 0644);
		auto rejected = [&] {
			bool failed = false;
			try {
				load();
			} catch (const std::exception &) {
				failed = true;
			}
			check(failed);
		};
#ifdef __APPLE__
		chmod_acl(configuration, "+a", "everyone allow write");
		struct stat acl_file{};
		check(::stat(configuration.c_str(), &acl_file) == 0 && (acl_file.st_mode & 0777) == 0600);
		rejected();
		chmod_acl(configuration, "-N");
		check(load().principal == "reader");
#endif
		{
			rapidjson::Document capacity;
			capacity.Parse(R"({"records":5000000,"wire_bytes":536870912,"image_bytes":536870912,
"decoded_bytes":536870912,"pages":12000000,"segments":2048})");
			object.AddMember("image_limits", rapidjson::Value(capacity, object.GetAllocator()),
							 object.GetAllocator());
			object.AddMember("heap_bytes", 1073741824, object.GetAllocator());
			object.AddMember("mapping_bytes", 1073741824, object.GetAllocator());
			write();
			const auto larger = load();
			check(larger.coordinator.source.limits.records == 5000000 &&
				  larger.coordinator.view_limits.records == 5000000 &&
				  larger.coordinator.view_limits.pages == 12000000 &&
				  larger.coordinator.view_limits.segments == 2048 &&
				  larger.coordinator.source.limits.storage_bytes == 536870912);
			for (auto value : {0ull, 4294967296ull}) {
				object["image_limits"]["records"].SetUint64(value);
				write();
				rejected();
			}
			object["image_limits"]["records"].SetUint(5000000);
			object["image_limits"]["decoded_bytes"].SetUint64(1073741825);
			write();
			rejected();
			object["image_limits"]["decoded_bytes"].SetUint(536870912);
			object["image_limits"]["records"].SetBool(true);
			write();
			rejected();
			object.RemoveMember("image_limits");
			object.RemoveMember("heap_bytes");
			object.RemoveMember("mapping_bytes");
			write();
			check(load().coordinator.source.limits.records == options.coordinator.source.limits.records);
		}
		object.AddMember("quorum_refresh_ms", 1000, object.GetAllocator());
		write();
		rejected();
		auto pool = [&](const char *json) {
			rapidjson::Document parsed;
			parsed.Parse(json);
			object.RemoveMember("authority_endpoints");
			object.AddMember("authority_endpoints", rapidjson::Value(parsed, object.GetAllocator()),
							 object.GetAllocator());
			write();
		};
		pool(R"([{"address":"127.0.0.1","port":8317},{"address":"127.0.0.1","port":8318}])");
		const auto pooled = load();
		check(pooled.coordinator.authority_endpoints.size() == 2 &&
			  pooled.coordinator.source.endpoint == pooled.coordinator.authority_endpoints.front() &&
			  pooled.coordinator.quorum_refresh == std::chrono::seconds(1));
		object.AddMember("port", 8317, object.GetAllocator());
		write();
		rejected();
		object.RemoveMember("port");
		for (const auto json :
			 {"[]", "{}", R"([{"address":"0.0.0.0"}])", R"([{"address":"127.0.0.1","port":0}])",
			  R"([{"address":"127.0.0.1"},{"address":"127.0.0.1"}])"}) {
			pool(json);
			rejected();
		}
		pool(R"([{"address":"127.0.0.1","port":8317}])");
		for (const auto interval : {0u, 999u, 86400001u}) {
			object["quorum_refresh_ms"].SetUint64(interval);
			write();
			rejected();
		}
		object["quorum_refresh_ms"].SetUint64(86400000);
		write();
		check(load().coordinator.quorum_refresh == std::chrono::hours(24));
		object.RemoveMember("quorum_refresh_ms");
		object.RemoveMember("authority_endpoints");
		write();
		using Format = detent::delivery::LocalImageFormat;
		check(options.coordinator.image_format == Format::PagedV2 &&
			  options.coordinator.compaction_interval == std::chrono::seconds(60));
		object.AddMember("image_format", "paged-v2", object.GetAllocator());
		object.AddMember("compaction_interval_ms", 0, object.GetAllocator());
		write();
		check(load().coordinator.image_format == Format::PagedV2 &&
			  load().coordinator.compaction_interval == std::chrono::milliseconds::zero());
		object["image_format"].SetString("unknown");
		write();
		rejected();
		object["image_format"].SetString("flat-v1");
		object["compaction_interval_ms"].SetUint64(86400000);
		write();
		check(load().coordinator.image_format == Format::FlatV1 &&
			  load().coordinator.compaction_interval == std::chrono::hours(24));
		for (const auto interval : {1u, 999u, 86400001u}) {
			object["compaction_interval_ms"].SetUint64(interval);
			write();
			rejected();
		}
		object.RemoveMember("image_format");
		object.RemoveMember("compaction_interval_ms");
		write();
		check(options.coordinator.delivery_mode == detent::delivery::DeliveryMode::Poll);
		check(options.principal == "reader" && options.policy_epoch == 1 && !options.delivery_mode_explicit);
		object.AddMember("delivery_mode", "subscription", object.GetAllocator());
		write();
		check(load().coordinator.delivery_mode == detent::delivery::DeliveryMode::Subscription);
		check(load().delivery_mode_explicit);
		object["delivery_mode"].SetString("unknown");
		write();
		rejected();
		object.RemoveMember("delivery_mode");
		write();
		std::filesystem::permissions(configuration, std::filesystem::perms::others_read,
									 std::filesystem::perm_options::add);
		rejected();
		write();
		std::filesystem::create_hard_link(configuration, directory / "linked");
		rejected();
		std::filesystem::remove(directory / "linked");
		std::filesystem::rename(configuration, directory / "original");
		std::filesystem::create_symlink(directory / "original", configuration);
		rejected();
		std::filesystem::remove(configuration);
		std::filesystem::rename(directory / "original", configuration);
		object.AddMember("port", 0, object.GetAllocator());
		write();
		rejected();
		object.RemoveMember("port");
		object.AddMember("compression", "false", object.GetAllocator());
		write();
		rejected();
		object.RemoveMember("compression");
		object.AddMember("heap_bytes", 1, object.GetAllocator());
		write();
		rejected();
		object.RemoveMember("heap_bytes");
		object.AddMember("artifact_mode", 0777, object.GetAllocator());
		write();
		rejected();
		object.RemoveMember("artifact_mode");
		object.AddMember("artifact_mode", 0640, object.GetAllocator());
		write();
		check(load().disk.artifact_mode == 0640);
		object.RemoveMember("artifact_mode");
		object.AddMember("principal", "duplicate", object.GetAllocator());
		write();
		rejected();
		std::filesystem::remove_all(directory);
		std::cout << "private managed configuration, canonical scope and strict rejection passed\n";
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
