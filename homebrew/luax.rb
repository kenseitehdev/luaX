class Luax < Formula
  desc "Extended Lua programming language"
  homepage "https://github.com/kenseitehdev/luaX"
  version "1.0.1"
  license "MIT" # Update this to your actual license
  
  if OS.mac?
    if Hardware::CPU.arm?
      url "https://github.com/kenseitehdev/luaX/releases/download/v1.0.1/luaX_MacOS_ARM"
      sha256 "a109e3441b6c1c37a3c659425723a320645cc185a0babf81da563edb2b681544" # Run: shasum -a 256 releases/v1.0.1/luaX_MacOS_ARM
    else
      # If you add Intel Mac support later
      odie "Intel Mac is not currently supported"
    end
  elsif OS.linux?
    url "https://github.com/kenseitehdev/luaX/releases/download/v1.0.1/luaX_Nix_x64"
    sha256 "7d878be3fc3d3b39867c017edc52a8da3e61e917b231ef96aa9436c122d33574" # Run: shasum -a 256 releases/v1.0.1/luaX_Nix_x64
  end

  def install
    if OS.mac?
      bin.install "luaX_MacOS_ARM" => "luax"
    elsif OS.linux?
      bin.install "luaX_Nix_x64" => "luax"
    end
    
    chmod 0755, bin/"luax"
  end

  test do
    assert_match "1.0.1", shell_output("#{bin}/luax --version")
  end
end
