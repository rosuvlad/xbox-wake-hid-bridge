"""Adds a `mergebin` target: pio run -e <env> -t mergebin

Fills $BUILD_DIR/release with a single image flashable at offset 0x0, the
individual images that went into it, and the offsets that place them.

Offsets and flash settings are read back out of the build environment instead
of being hardcoded, so a merged image always lands where `pio run -t upload`
would have put the same bytes.
"""

import os
import shutil

Import("env")

board = env.BoardConfig()


def _flash_images():
    """Every image the board needs, as (offset, path), lowest offset first."""
    images = [
        (env.subst(offset), env.subst(path))
        for offset, path in env.get("FLASH_EXTRA_IMAGES", [])
    ]
    if not images:
        raise Exception(
            "The build environment lists no FLASH_EXTRA_IMAGES, so the bootloader "
            "and partition offsets are unknown and a merged image would be wrong."
        )
    images.append(
        (env.subst("$ESP32_APP_OFFSET"), env.subst("$BUILD_DIR/${PROGNAME}.bin"))
    )
    return sorted(images, key=lambda image: int(image[0], 16))


def _asset_name(path):
    # The bootloader is built per flash mode/speed (bootloader_qio_80m.bin and
    # friends); release assets read better with the variant dropped.
    name = os.path.basename(path)
    return "bootloader.bin" if name.startswith("bootloader") else name


def merge_bin(target, source, env):
    release_dir = env.subst("$BUILD_DIR/release")
    shutil.rmtree(release_dir, ignore_errors=True)
    os.makedirs(release_dir)

    images = _flash_images()
    for offset, path in images:
        if not os.path.isfile(path):
            raise Exception("Flash image for offset %s is missing: %s" % (offset, path))
        shutil.copy(path, os.path.join(release_dir, _asset_name(path)))

    placements = ["%s %s" % (offset, _asset_name(path)) for offset, path in images]
    with open(os.path.join(release_dir, "flash-args.txt"), "w") as handle:
        handle.write(" ".join(placements) + "\n")

    command = [
        '"$PYTHONEXE"',
        '"$OBJCOPY"',
        "--chip",
        board.get("build.mcu", "esp32"),
        "merge_bin",
        "-o",
        '"%s"' % os.path.join(release_dir, "merged.bin"),
        "--flash_mode",
        "${__get_board_flash_mode(__env__)}",
        "--flash_freq",
        "${__get_board_f_image(__env__)}",
        "--flash_size",
        board.get("upload.flash_size", "4MB"),
    ]
    for offset, path in images:
        command.extend([offset, '"%s"' % path])

    status = env.Execute(" ".join(command))
    if status != 0:
        raise Exception("esptool merge_bin failed with exit code %s" % status)

    print("Merged image and parts written to %s" % release_dir)


env.AddCustomTarget(
    name="mergebin",
    dependencies=["$BUILD_DIR/${PROGNAME}.bin", "$BUILD_DIR/partitions.bin"],
    actions=[merge_bin],
    title="Merge flash image",
    description="Bundle bootloader, partitions and app into release/merged.bin (flash at 0x0)",
)
