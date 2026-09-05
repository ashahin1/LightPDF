#!/usr/bin/env python3

import io
import os
import sys
import stat
import json
import shutil
import hashlib
import zipfile
import tempfile
import argparse
import subprocess
import urllib.error
import urllib.request
from pathlib import Path

OUTPUT = Path("msvc")         # output folder
DOWNLOADS = Path("downloads") # temporary download files

DEFAULT_HOST = "x64"
ALL_HOSTS    = "x64 x86 arm64".split()

DEFAULT_TARGET = "x64"
ALL_TARGETS    = "x64 x86 arm arm64".split()

DEFAULT_VERSION = "latest"
ALL_VERSIONS    = "2019 2022 2026 latest".split()

MANIFEST_URLS = {
  "latest": ["https://aka.ms/vs/stable/channel",     "https://aka.ms/vs/insiders/channel"   ],
  "2026":   ["https://aka.ms/vs/18/stable/channel",  "https://aka.ms/vs/18/insiders/channel"],
  "2022":   ["https://aka.ms/vs/17/release/channel", "https://aka.ms/vs/17/pre/channel"     ],
  "2019":   ["https://aka.ms/vs/16/release/channel", "https://aka.ms/vs/16/pre/channel"     ],
}

ssl_context = None

def download(url):
  with urllib.request.urlopen(url, context=ssl_context) as res:
    return res.read()

total_download = 0

def download_progress(url, check, filename):
  fpath = DOWNLOADS / filename
  if fpath.exists():
    data = fpath.read_bytes()
    if hashlib.sha256(data).hexdigest() == check.lower():
      print(f"\r{filename} ... OK")
      return data

  global total_download
  with fpath.open("wb") as f:
    data = io.BytesIO()
    with urllib.request.urlopen(url, context=ssl_context) as res:
      total = int(res.headers.get("Content-Length", 0))
      size = 0
      while True:
        block = res.read(1<<20)
        if not block:
          break
        f.write(block)
        data.write(block)
        size += len(block)
        perc = (size * 100 // total) if total > 0 else 0
        print(f"\r{filename} ... {perc}%", end="")
    print()
    data = data.getvalue()
    digest = hashlib.sha256(data).hexdigest()
    if check.lower() != digest:
      sys.exit(f"Hash mismatch for {filename}")
    total_download += len(data)
    return data

def get_msi_cabs(msi):
  index = 0
  while True:
    index = msi.find(b".cab", index+4)
    if index < 0:
      return
    yield msi[index-32:index+4].decode("ascii")

def first(items, cond = lambda x: True):
  return next((item for item in items if cond(item)), None)


ap = argparse.ArgumentParser()
ap.add_argument("--show-versions", action="store_true", help="Show available MSVC and Windows SDK versions")
ap.add_argument("--accept-license", action="store_true", help="Automatically accept license")
ap.add_argument("--msvc-version", help="Get specific MSVC version")
ap.add_argument("--sdk-version", help="Get specific Windows SDK version")
ap.add_argument("--vs", default=DEFAULT_VERSION, help="Visual Studio version to use for installation", choices=ALL_VERSIONS)
ap.add_argument("--insiders", action="store_true", help="Use insiders channel")
ap.add_argument("--preview", action="store_true", help="Allow preview / release candidate MSVC versions")
ap.add_argument("--target", default=DEFAULT_TARGET, help=f"Target architectures, comma separated ({','.join(ALL_TARGETS)})")
ap.add_argument("--host", default=DEFAULT_HOST, help="Host architecture", choices=ALL_HOSTS)
args = ap.parse_args()

host = args.host
targets = args.target.split(',')
for target in targets:
  if target not in ALL_TARGETS:
    sys.exit(f"Unknown {target} target architecture!")

URL = MANIFEST_URLS[args.vs][args.insiders]

try:
  manifest = json.loads(download(URL))
except urllib.error.URLError as err:
  import ssl
  if isinstance(err.args[0], ssl.SSLCertVerificationError):
    print("ERROR: ssl certificate verification error")
    try:
      import certifi
    except ModuleNotFoundError:
      print("ERROR: please install 'certifi' package to use Mozilla certificates")
      sys.exit()
    print("NOTE: retrying with certifi certificates")
    ssl_context = ssl.create_default_context(cafile=certifi.where())
    manifest = json.loads(download(URL))
  else:
    raise

for item in manifest["channelItems"]:
  if item["type"].lower() == "manifest":
    manifest = json.loads(download(item["payloads"][0]["url"]))
    break
else:
  sys.exit("Could not find manifest!")

packages = {}
for p in manifest["packages"]:
  packages.setdefault(p["id"].lower(), []).append(p)

def get_msvc_versions():
  res = []
  for pid, pkgs in packages.items():
    if pid.startswith("microsoft.vc.tools.host"):
      p = first(pkgs, lambda p: p.get("language") in (None, "en-US"))
      if p:
        ver = p["version"]
        res.append((pid, ver))
  return res

msvc_versions = {}
for pid, ver in get_msvc_versions():
  p = first(packages[pid], lambda p: p.get("language") in (None, "en-US"))
  if p:
    msvc_versions[p["version"]] = p

sdk_versions = {}
for pid, pkgs in packages.items():
  if pid.startswith("microsoft.windows.sdk.desktop"):
    p = first(pkgs, lambda p: p.get("language") in (None, "en-US"))
    if p:
      sdk_versions[p["version"]] = p

if args.show_versions:
  print("MSVC versions:")
  for ver in sorted(msvc_versions.keys()):
    print(f"  {ver}")
  print("\nWindows SDK versions:")
  for ver in sorted(sdk_versions.keys()):
    print(f"  {ver}")
  sys.exit()

if args.msvc_version:
  msvc_ver = args.msvc_version
  if msvc_ver not in msvc_versions:
    sys.exit(f"Unknown MSVC version: {msvc_ver}")
else:
  msvc_ver = sorted(msvc_versions.keys())[-1]

if args.sdk_version:
  sdk_ver = args.sdk_version
  if sdk_ver not in sdk_versions:
    sys.exit(f"Unknown Windows SDK version: {sdk_ver}")
else:
  sdk_ver = sorted(sdk_versions.keys())[-1]

print(f"Selected MSVC version: {msvc_ver}")
print(f"Selected Windows SDK version: {sdk_ver}")

if not args.accept_license:
  ans = input("Do you accept the Microsoft license terms for MSVC and Windows SDK? (y/n): ")
  if ans.strip().lower() not in ("y", "yes"):
    sys.exit("Aborted.")

DOWNLOADS.mkdir(exist_ok=True)
OUTPUT.mkdir(exist_ok=True)

# 1. Download & extract MSVC core tools
msvc_p = msvc_versions[msvc_ver]
msvc_pkgs = [
  f"microsoft.vc.tools.host{host}.target{target}.base" for target in targets
] + [
  f"microsoft.vc.tools.host{host}.target{host}.res.base",
  "microsoft.vc.tools.crt.headers.base",
]
for target in targets:
  msvc_pkgs.append(f"microsoft.vc.tools.crt.{target}.desktop.base")
  msvc_pkgs.append(f"microsoft.vc.tools.crt.{target}.store.base")

print("\n--- Downloading MSVC packages ---")
for pid in msvc_pkgs:
  pkgs = packages.get(pid.lower(), [])
  p = first(pkgs, lambda x: x["version"] == msvc_ver and x.get("language") in (None, "en-US"))
  if not p:
    p = first(pkgs, lambda x: x.get("language") in (None, "en-US"))
  if not p:
    print(f"Skipping missing package: {pid}")
    continue
  payload = p["payloads"][0]
  fname = payload["fileName"].replace("\\", "_")
  data = download_progress(payload["url"], payload["sha256"], fname)
  with zipfile.ZipFile(io.BytesIO(data)) as z:
    for member in z.infolist():
      if not member.is_dir():
        norm_path = member.filename.replace("\\", "/")
        out_file = OUTPUT / norm_path
        out_file.parent.mkdir(parents=True, exist_ok=True)
        with z.open(member) as src, open(out_file, "wb") as dst:
          shutil.copyfileobj(src, dst)

print("\n--- Downloading Windows SDK packages ---")
sdk_p = sdk_versions[sdk_ver]
sdk_deps = sdk_p.get("dependencies", {})
sdk_pkg_id = first(sdk_deps, lambda d: "desktop" in d.lower())
if not sdk_pkg_id:
  sdk_pkg_id = list(sdk_deps.keys())[0]

sdk_base = packages[sdk_pkg_id.lower()][0]

sdk_msi_files = [
  "Windows SDK Desktop Headers x86-en-us.msi",
  "Windows SDK Desktop Headers x64-en-us.msi",
  "Windows SDK Desktop Libs x64-en-us.msi",
  "Windows SDK OnecoreUap Headers x64-en-us.msi",
  "Windows SDK OnecoreUap Headers x86-en-us.msi",
]

msis = []
cabs = []
for f in sdk_base.get("payloads", []):
  fname = Path(f["fileName"]).name
  if fname in sdk_msi_files:
    data = download_progress(f["url"], f["sha256"], fname)
    msis.append(DOWNLOADS / fname)
    cabs.extend(list(get_msi_cabs(data)))

cabs = sorted(set(cabs))
for f in sdk_base.get("payloads", []):
  fname = Path(f["fileName"]).name
  if fname in cabs:
    download_progress(f["url"], f["sha256"], fname)

print("\nUnpacking Windows SDK MSIs...")
for m in msis:
  cmd = f'msiexec /a "{m.resolve()}" /quiet /qn TARGETDIR="{OUTPUT.resolve()}"'
  subprocess.check_call(cmd, shell=True)
  m_in_out = OUTPUT / m.name
  if m_in_out.exists():
    m_in_out.unlink()

# Move nested Windows Kits folder if present
pf = OUTPUT / "Program Files"
if (pf / "Windows Kits").exists():
  for src_dir, _, files in os.walk(pf / "Windows Kits"):
    rel = Path(src_dir).relative_to(pf)
    target_dir = OUTPUT / rel
    target_dir.mkdir(parents=True, exist_ok=True)
    for file in files:
      sfile = Path(src_dir) / file
      dfile = target_dir / file
      sfile.replace(dfile)
  shutil.rmtree(pf, ignore_errors=True)

# Locate installed versions
msvc_dir = first((OUTPUT / "VC/Tools/MSVC").glob("*"))
sdk_dir = first((OUTPUT / "Windows Kits/10/bin").glob("10.*"))

if not msvc_dir or not sdk_dir:
  sys.exit("Error: Could not find extracted MSVC or Windows SDK directories.")

msvcv = msvc_dir.name
sdkv = sdk_dir.name

print(f"\nExtracted MSVC Version: {msvcv}")
print(f"Extracted Windows SDK Version: {sdkv}")

# Generate setup_x64.bat
setup_content = f"""@echo off
set VSCMD_ARG_HOST_ARCH=x64
set VSCMD_ARG_TGT_ARCH=x64
set VCToolsVersion={msvcv}
set WindowsSDKVersion={sdkv}\\
set VCToolsInstallDir=%~dp0VC\\Tools\\MSVC\\{msvcv}\\
set WindowsSdkBinPath=%~dp0Windows Kits\\10\\bin\\
set PATH=%~dp0VC\\Tools\\MSVC\\{msvcv}\\bin\\Hostx64\\x64;%~dp0Windows Kits\\10\\bin\\{sdkv}\\x64;%PATH%
set INCLUDE=%~dp0VC\\Tools\\MSVC\\{msvcv}\\include;%~dp0Windows Kits\\10\\Include\\{sdkv}\\ucrt;%~dp0Windows Kits\\10\\Include\\{sdkv}\\shared;%~dp0Windows Kits\\10\\Include\\{sdkv}\\um;%~dp0Windows Kits\\10\\Include\\{sdkv}\\winrt;%~dp0Windows Kits\\10\\Include\\{sdkv}\\cppwinrt
set LIB=%~dp0VC\\Tools\\MSVC\\{msvcv}\\lib\\x64;%~dp0Windows Kits\\10\\Lib\\{sdkv}\\ucrt\\x64;%~dp0Windows Kits\\10\\Lib\\{sdkv}\\um\\x64
echo [portable-msvc] MSVC {msvcv} and Windows SDK {sdkv} (x64) activated.
"""

(OUTPUT / "setup_x64.bat").write_text(setup_content, encoding="utf-8")
print(f"\nCreated: {OUTPUT / 'setup_x64.bat'}")
print("Done! Portable MSVC environment is ready.")
