# DB-REbuilder

DB-REbuilder is a payload designed for jailbroken PS4 consoles. It resolves the issue of applications disappearing from the main menu.

## Download

To download the latest version, please visit the [Releases Page](https://github.com/m2k7m/DB-REbuilder/releases/latest).

## How to Run

### Method 1: Network Injection
Send the payload over your local network using one of these tools:
* The [`payload_sender.py`](https://github.com/m2k7m/DB-REbuilder/blob/main/payload_sender.py) script included in this repository.
* [Al-Azif/hermes-link](https://github.com/Al-Azif/hermes-link/releases/latest).

### Method 2: On-Console Execution
Transfer the payload to `/data/payloads/` using FTP, or place it on a USB drive at `usb/payloads/`. Then, launch it using one of the following:
* [GoldHEN v2.4b18.10+](https://ko-fi.com/s/d64a916507)
* [Vuemony/vue-after-free](https://github.com/Vuemony/vue-after-free/releases/latest) (Full version)
* [Al-Azif/ps4-payload-guest](https://github.com/Al-Azif/ps4-payload-guest/releases/latest)

## How to Use

Simply [run the payload](https://github.com/m2k7m/DB-REbuilder#how-to-run), then open and close the Internet Browser on your console to refresh the main menu.

## How to Build

Requires [WSL](https://learn.microsoft.com/en-us/windows/wsl/install) with a Ubuntu distro.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

On first run it automatically downloads and builds the [ps4-payload-dev/sdk](https://github.com/ps4-payload-dev/sdk), then compiles the payload (`db-rebuilder-v*.elf` / `.bin`) into this folder. Run `.\build.ps1 -CleanSdk` to force a rebuild of the SDK itself.

<details>
<summary>Native Linux</summary>

One-time SDK setup:

```console
sudo apt install -y bash clang lld make git xxd
git clone https://github.com/ps4-payload-dev/sdk
make -C sdk DESTDIR=$HOME/ps4-payload-sdk clean install
```

Build the payload:

```console
export PS4_PAYLOAD_SDK=$HOME/ps4-payload-sdk
make clean all
```
</details>


## Credits

* A huge thanks to [bucanero](https://github.com/bucanero/) for his awesome [apollo-ps4](https://github.com/bucanero/apollo-ps4/tree/main) project.
* A huge thanks to [John Törnblom](https://github.com/john-tornblom) and [ps4-payload-dev](https://github.com/ps4-payload-dev/) for [sdk](https://github.com/ps4-payload-dev/sdk) and [elfldr](https://github.com/ps4-payload-dev/elfldr).
* Developed by me and the 4GAMER team.
