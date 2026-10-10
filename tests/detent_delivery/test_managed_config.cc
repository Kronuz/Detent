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
		check(options.coordinator.delivery_mode == detent::delivery::DeliveryMode::Poll);
		object.AddMember("delivery_mode", "subscription", object.GetAllocator());
		write();
		check(load().coordinator.delivery_mode == detent::delivery::DeliveryMode::Subscription);
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
