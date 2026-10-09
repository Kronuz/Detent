# Homebrew formula for Detent (build-from-source).
#
# This is the canonical source for the formula. To publish it, copy it into the
# tap repository the docs point at (github.com/Kronuz/homebrew-tap) as
# Formula/detent.rb, so `brew install Kronuz/tap/detent` resolves.
#
# Detent has no tagged releases yet, so the stable build pins a revision of
# main that is known to build from public sources; bump `revision` and
# `version` together. `brew install --HEAD Kronuz/tap/detent` tracks main.
class Detent < Formula
  desc "Runtime knobs: feature flags, kill switches and live settings"
  homepage "https://kronuz.github.io/Detent/"
  url "https://github.com/Kronuz/Detent.git",
      branch:   "main",
      revision: "323b9719ec8ba7ec6031745c729b54e79fc34e33"
  version "0.1.0-dev.20261009"
  head "https://github.com/Kronuz/Detent.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "lz4"
  depends_on "openssl@3"
  depends_on "zstd"

  uses_from_macos "zlib"

  def install
    args = %W[
      -DCMAKE_BUILD_TYPE=Release
      -DDETENT_BUILD_TESTS=OFF
      -DDETENT_BUILD_EXAMPLES=OFF
      -DDETENT_BUILD_BENCHMARKS=OFF
      -DHOMEBREW_ALLOW_FETCHCONTENT=ON
      -DOPENSSL_ROOT_DIR=#{Formula["openssl@3"].opt_prefix}
    ]
    # Homebrew builds against the SDK matching the running macOS. Before macOS 26
    # that SDK ships libc++ 19, which keeps std::jthread and stop tokens behind
    # its experimental library; Detent's configure check requires them.
    args << "-DCMAKE_CXX_FLAGS=-fexperimental-library" if OS.mac? && MacOS.version < :tahoe

    # Detent fetches its Kronuz-family dependencies (and standalone Asio) with
    # FetchContent, which Homebrew traps by default. HOMEBREW_ALLOW_FETCHCONTENT
    # is Homebrew's sanctioned opt-out, the same one the Xapiand formula uses.
    system "cmake", "-S", ".", "-B", "build", "-GNinja", *args, *std_cmake_args
    system "cmake", "--build", "build", "--parallel", ENV.make_jobs.to_s
    system "cmake", "--install", "build"
  end

  def caveats
    <<~EOS
      Start a distributor in the background and read a knob:
        detentd --daemonize
        detent get NAME

      Service templates for launchd are in:
        #{opt_pkgshare}/launchd
    EOS
  end

  test do
    (testpath/"defaults.json").write <<~JSON
      {"schema_version": 1, "knobs": {"app:on": {"type": "bool", "default": true}}}
    JSON
    runtime = testpath/"runtime"
    assert_equal "true\n",
                 shell_output("#{bin}/detent get app:on --defaults #{testpath}/defaults.json " \
                              "--runtime-dir #{runtime}")
  ensure
    status = Utils.safe_popen_read(bin/"detent-agent", "--status", "--runtime-dir", runtime.to_s)
    pid = status[/running \(pid (\d+)\)/, 1]
    Process.kill("TERM", pid.to_i) if pid
  end
end
