class Websockify2 < Formula
  desc "High-performance WebSocket-to-TCP proxy written in C"
  homepage "https://github.com/zhlynn/websockify2"
  license "MIT"
  head "https://github.com/zhlynn/websockify2.git", branch: "main"

  # For tagged releases, override url/sha256:
  # url "https://github.com/zhlynn/websockify2/archive/refs/tags/v1.0.0.tar.gz"
  # sha256 "REPLACE_ME"
  # version "1.0.0"

  depends_on "openssl@3"

  def install
    # Detect OpenSSL via pkg-config or fall back to Homebrew's path
    ENV["PKG_CONFIG_PATH"] = "#{Formula["openssl@3"].opt_lib}/pkgconfig"

    system "sh", "configure.sh"
    system "make", "all"

    bin.install "build/bin/websockify2"
  end

  test do
    # --version must exit cleanly and print the expected name
    assert_match "websockify2", shell_output("#{bin}/websockify2 --version")

    # --help must succeed
    assert_match "Usage:", shell_output("#{bin}/websockify2 --help")

    # Reject missing arguments
    assert_match "required", shell_output("#{bin}/websockify2 2>&1", 1)

    # Smoke test: start the server, confirm it binds, shut it down
    port = free_port
    pid = spawn bin/"websockify2", port.to_s, "127.0.0.1:1"
    sleep 1
    Process.kill("TERM", pid)
    Process.wait(pid)
  end
end
