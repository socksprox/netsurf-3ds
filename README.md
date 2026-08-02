# Netsurf 3DS

Work in progress homebrew 3DS port of the lightweight [NetSurf web browser](https://www.netsurf-browser.org/).

# Installing 

If you want to try it out in its current state, you can find the latest version as a 3DSX file on the [releases page](https://github.com/coderman64/netsurf-3ds/releases). You will also need to download the resources.zip package, and extract the contents it to `/share/netsurf` on your 3DS's SD card. **NetSurf will not run properly without the resources**.

## Why not use the default web browser?

Nintendo's 3DS browser is based on an old version of Apple's WebKit, and only supports SSL up to 1.1, which many websites are already dropping. Even among websites that it *can* load, it often hopelessly mangles them. It also doesn't support downloading anything but JPEG images to the SD card, which is unhelpful for downloading homebrew.

This port, while a work in progress, already supports SSL 1.2, and has the potential to be a much better option than the default browser as development continues.

## Why port NetSurf?

The 3DS (especially the original version) is a very slow, low-memory system (only 64MB is provided to most apps, something more like 80MB to HIMEM apps), so many modern web engines would require an obscene amount of modification to even start running on the handheld. 

This is where NetSurf comes in. It is a lightweight, super fast web browser that already can run on systems with memory requirements as low as 32MB. It has a minimal amount of dependencies, and even has a built-in "framebuffer" backend that doesn't require any external toolkits, and it has fairly good support for modern HTML and CSS standards given the constraints.

## What it can do:
- browse websites with up to SSL 1.2
- browse the SD card and open local text, html, and image files (go to file:///)
- html and css support
- basic JavaScript support (Duktape; enable in app menu)
- Use the 80MB HiMem mode through `nsfb_himem.cia`

## What it might be able to do soon&trade;:

The NetSurf codebase contains some features that don't work yet on 3DS, but might work soon with further porting efforts.
- Font support (FreeType)
- super basic JavaScript support (via internal "duktape" library) — toggle in app menu or Settings
- SVG and WebP support (requires external libraries to be ported first)
- Webpage zoom (+/-)

Here are some things not in the code base, but are planned to be added:
- Downloading of files to the SD card (perhaps with a "save as" dialog)
- Additional 3DS-specific button controls

## Known issues
- SSL implementation is partially broken (i.e. you can visit SSL websites, but you can't view the SSL certificate information)
- some things (e.g. larger images) distort while scrolling.
- Text is often cut off or wraps incorrectly
- JavaScript is experimental; enable via app menu or Settings (uses extra memory; HiMem recommended)
- Support for several media types is disabled. (SVG, WebP)

## What it will probably never do:
NetSurf is a long way off from a Google Chrome replacement, and (given the processing and memory constraints) will never be close. Here are some things that people might ask about that are probably impossible:
- Playing HTML5/WebGL, Java, or Flash games
- Playing YouTube videos or streaming movies (even though NetSurf has experimental video support in the code).
- Loading and running complex Web 2.0 applications like Google Docs, VS Code, Discord, etc.
- super accurate website rendering on par with desktop and mobile phone browsers.

# Building 

Requires [devkitPro](https://devkitpro.org/) with DevKitARM, libctru, and the 3DS portlibs listed below. macOS and Linux are supported.

Install the required portlibs (once):

```bash
sudo dkp-pacman -Sy 3ds-sdl 3ds-mbedtls 3ds-curl 3ds-libpng 3ds-libjpeg-turbo 3ds-libiconv 3ds-freetype
```

On macOS you also need GNU Bison (`brew install bison`).

Build from the repository root:

```bash
make
```

This produces `nsfb.3dsx` in the project root (and `netsurf/nsfb.3dsx`).

Deploy over the network to a 3DS running the homebrew launcher (requires [3dslink](https://github.com/devkitPro/3dslink)):

```bash
make deploy
```

Set your 3DS IP address with `THREE_DS_IP` if needed (default: `192.168.86.230`):

```bash
make deploy THREE_DS_IP=192.168.1.42
```

To also build installable CIA files, install `makerom` from [Project_CTR](https://github.com/3DSGuy/Project_CTR/releases) and run `make build NOCIA=` (CIAs are skipped by default).

## Special thanks

Thanks to the [NetSurf developers](https://www.netsurf-browser.org/) for making such an awesome lightweight and portable browser! Also thanks to [DevKitPro](https://devkitpro.org/) for making the 3DS homebrew toolchain and library ports.
