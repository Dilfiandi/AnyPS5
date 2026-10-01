import argparse
import re
import shutil
import tarfile
import zipfile
from pathlib import Path


RUNTIME_DLLS = ("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")


def validate_version(version):
    if not re.fullmatch(r"v[0-9A-Za-z][0-9A-Za-z._-]*", version) or version.endswith("."):
        raise ValueError(f"Invalid release tag for asset filenames: {version}")


def collect_runtime_files(build):
    libraries = sorted((build / "core/libs/libs").glob("*.prx"))
    expected = {f"{directory.name}.prx" for directory in Path("core/libs/prx").iterdir() if directory.is_dir()}
    expected.add("libcohtml.Prospero.prx")
    missing = expected - {library.name for library in libraries}
    if missing:
        raise RuntimeError(f"Missing patched libraries: {', '.join(sorted(missing))}")

    runtime = Path("C:/winlibs/mingw64/bin")
    runtime_files = [runtime / name for name in RUNTIME_DLLS]
    files = [*libraries, *runtime_files]
    for file in files:
        if not file.is_file() or file.stat().st_size == 0:
            raise RuntimeError(f"Missing or empty release file: {file}")
    return files


def package_windows(build, output, version):
    validate_version(version)

    runtime_files = collect_runtime_files(build)
    relinker = build / "core/relinker/relinker.exe"
    gui = build / "core/gui/AnyPS5.exe"

    for file in (relinker, gui):
        if not file.is_file() or file.stat().st_size == 0:
            raise RuntimeError(f"Missing or empty release file: {file}")

    output.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(
        output / f"prx-windows-{version}.zip",
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:
        for file in runtime_files:
            archive.write(file, arcname=f"libs/{file.name}")

    with tarfile.open(output / f"prx-windows-{version}.tar.gz", "w:gz", compresslevel=9) as archive:
        for file in runtime_files:
            archive.add(file, arcname=f"libs/{file.name}")

    shutil.copy2(relinker, output / f"relinker-{version}.exe")

    portable_root = f"AnyPS5-portable-{version}"
    with zipfile.ZipFile(
        output / f"{portable_root}.zip",
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:
        archive.write(gui, arcname=f"{portable_root}/AnyPS5.exe")
        archive.write(relinker, arcname=f"{portable_root}/relinker.exe")
        for file in runtime_files:
            archive.write(file, arcname=f"{portable_root}/libs/{file.name}")

        for document in (Path("README.md"), Path("LICENSE")):
            archive.write(document, arcname=f"{portable_root}/{document.name}")

        for document in sorted(Path("docs/user").glob("*.md")):
            archive.write(document, arcname=f"{portable_root}/docs/{document.name}")


def collect_docs(source, output):
    documents = sorted(source.rglob("*.md"))
    if not documents:
        raise RuntimeError(f"No Markdown documents found in {source}")
    names = set()
    for document in documents:
        if document.name in names or (output / document.name).exists():
            raise RuntimeError(f"Duplicate release asset name: {document.name}")
        names.add(document.name)
    output.mkdir(parents=True, exist_ok=True)
    for document in documents:
        shutil.copy2(document, output / document.name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path)
    parser.add_argument("--version")
    parser.add_argument("--docs", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    if args.docs is not None:
        if args.build is not None or args.version is not None:
            parser.error("--docs cannot be combined with --build or --version")
        collect_docs(args.docs, args.output)
    else:
        if args.build is None or args.version is None:
            parser.error("--build and --version are required when --docs is not specified")
        package_windows(args.build, args.output, args.version)


if __name__ == "__main__":
    main()
