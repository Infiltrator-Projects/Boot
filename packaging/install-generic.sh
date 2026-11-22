#!/usr/bin/env bash
# BOOT / Generic package installation
#
# Installs the prebuilt EFI application, filesystem drivers, configuration
# and artwork during Debian package configuration.
#
# The destination is fixed. Copying ESP resources and registering firmware
# BootOrder remain separate, externally observable operations.

set -euo pipefail

# Check the exact package payload before touching the ESP: a prebuilt EFI
# loader, six filesystem drivers, configuration/icons and all 23 manifest-
# pinned TrueType faces. This is file presence/font integrity verification,
# not EFI executable signature or controller-binding qualification.
boot_generic_payload_check() {
    local source="$1" filesystem
    [[ -s "$source/boot_x64.efi" && -s "$source/boot.conf-sample" && -d "$source/icons" ]] || {
        echo "BOOT generic package is missing the EFI image, configuration or icons." >&2
        return 1
    }
    local face
    local fontdir="$source/fonts" count
    [[ -f "$fontdir/SHA256SUMS" && ! -L "$fontdir/SHA256SUMS" ]] || {
        echo "BOOT generic package has no font checksum manifest." >&2
        return 1
    }
    count=$(find "$fontdir" -maxdepth 1 -type f -name '*.ttf' | wc -l)
    [[ "$count" -eq 23 && $(wc -l < "$fontdir/SHA256SUMS") -eq 23 ]] || {
        echo "BOOT generic package requires all 23 MB/Daimler fonts." >&2
        return 1
    }
    ( cd "$fontdir" && sha256sum -c SHA256SUMS >/dev/null ) || {
        echo "BOOT generic package font integrity check failed." >&2
        return 1
    }
    for filesystem in ext2 ext4 reiserfs iso9660 hfs btrfs; do
        [[ -s "$source/drivers_x64/${filesystem}_x64.efi" ]] || {
            echo "BOOT generic package is missing the $filesystem EFI driver." >&2
            return 1
        }
    done
}

# Resolve a mounted VFAT ESP with EFI partition type, parent disk, numeric
# partition number and PARTUUID. These device identifiers must all describe
# the same selected partition before firmware registration is attempted.
boot_generic_esp() {
    local candidate source filesystem partition_type disk part uuid
    [[ -d /sys/firmware/efi ]] || {
        echo "BOOT requires a running UEFI Linux system for firmware registration." >&2
        return 1
    }
    for candidate in /boot/efi /efi /boot; do
        filesystem=$(findmnt -rn --mountpoint "$candidate" -o FSTYPE 2>/dev/null || :)
        [[ "$filesystem" == vfat ]] || continue
        source=$(findmnt -rn --mountpoint "$candidate" -o SOURCE 2>/dev/null || :)
        [[ "$source" == /dev/* ]] || continue
        partition_type=$(lsblk -dn -o PARTTYPE "$source" 2>/dev/null | head -n 1 | tr '[:upper:]' '[:lower:]')
        case "$partition_type" in
            c12a7328-f81f-11d2-ba4b-00a0c93ec93b|0xef) ;;
            *) continue ;;
        esac
        disk=$(lsblk -dn -o PKNAME "$source" 2>/dev/null | head -n 1)
        part=$(lsblk -dn -o PARTN "$source" 2>/dev/null | head -n 1)
        uuid=$(blkid -s PARTUUID -o value "$source" 2>/dev/null || :)
        [[ "$disk" =~ ^[a-zA-Z0-9_.+-]+$ && "$part" =~ ^[0-9]+$ && -n "$uuid" ]] || continue
        BOOT_GENERIC_ESP="$candidate"
        BOOT_GENERIC_DISK="/dev/$disk"
        BOOT_GENERIC_PART="$part"
        BOOT_GENERIC_PARTUUID="${uuid,,}"
        return 0
    done
    echo "BOOT could not identify a mounted VFAT EFI System Partition at /boot/efi, /efi or /boot." >&2
    return 1
}

# Replace only BOOT's fixed EFI directory after checking relevant parent
# symlinks. This intentionally destroys old BOOT configuration and resources
# while leaving independent EFI vendor directories outside this path.
boot_generic_install_files() {
    local esp="$1" source="$2" destination="$1/EFI/InfiltratorOS/BOOT" filesystem asset
    boot_generic_payload_check "$source" || return 1
    [[ -d "$esp/EFI" ]] || { echo "BOOT requires a valid ESP directory." >&2; return 1; }
    if [[ -L "$esp/EFI" || -L "$esp/EFI/InfiltratorOS" || -L "$destination" ]]; then
        echo "BOOT refuses a symbolic link in its fixed EFI installation path." >&2
        return 1
    fi
    # The entire reserved BOOT directory is replaced, regardless of its prior
    # contents. No paths belonging to Windows, GRUB or the EFI fallback change.
    rm -rf -- "$destination"
    install -d -m 0755 "$destination/drivers_x64"
    install -m 0644 "$source/boot_x64.efi" "$destination/BOOTX64.EFI"
    for filesystem in ext2 ext4 reiserfs iso9660 hfs btrfs; do
        install -m 0644 "$source/drivers_x64/${filesystem}_x64.efi" "$destination/drivers_x64/"
    done
    install -m 0644 "$source/boot.conf-sample" "$destination/boot.conf"
    for asset in icons fonts banners themes keys; do
        if [[ -d "$source/$asset" ]]; then
            cp -a -- "$source/$asset" "$destination/"
        fi
    done
    if command -v iconv >/dev/null 2>&1; then
        printf 'BOOTX64.EFI,BOOT Boot Manager,,This is the boot entry for BOOT\n' |
            iconv -t UCS-2 > "$destination/BOOT.CSV"
    fi
    [[ -s "$destination/BOOTX64.EFI" ]] || return 1
}

# Require exactly one firmware entry matching BOOT's pathname and the
# selected ESP identity. Activation and BootOrder promotion are separate
# firmware writes, so a failure between them can leave partial NVRAM state.
# Re-reading BootOrder checks one effect but is not an atomic transaction.
boot_generic_register() {
    local disk="$1" partition="$2" uuid="$3"
    local listing line lower number boot_number="" match_count total created=0
    local order new_order item verified
    local -a numbers=()
    local loader_path='\EFI\InfiltratorOS\BOOT\BOOTX64.EFI'
    command -v efibootmgr >/dev/null 2>&1 || {
        echo "BOOT requires efibootmgr to register its UEFI boot entry." >&2
        return 1
    }
    match_count=0
    total=0
    # Re-inspect after creating an entry rather than assuming a firmware
    # variable write was accepted or that a particular Boot#### was allocated.
    while :; do
        listing=$(efibootmgr -v) || return 1
        [[ "$listing" == *"BootOrder:"* ]] || {
            echo "BOOT firmware returned no BootOrder." >&2
            return 1
        }
        match_count=0
        total=0
        boot_number=""
        while IFS= read -r line; do
            if [[ "$line" =~ ^Boot([[:xdigit:]]{4})\*?[[:space:]] ]]; then
                number="${BASH_REMATCH[1]^^}"
                lower="${line,,}"
                if [[ "$lower" == *"file(${loader_path,,})"* ||
                      "$lower" == *"boot boot manager"* ]]; then
                    ((total+=1))
                    if [[ "$lower" == *"file(${loader_path,,})"* &&
                          "$lower" == *"${uuid,,}"* ]]; then
                        ((match_count+=1))
                        boot_number="$number"
                    fi
                fi
            fi
        done <<< "$listing"
        if (( match_count == 1 && total == 1 )); then
            break
        fi
        if (( total > 0 )); then
            echo "BOOT UEFI entry is duplicate, stale or ambiguous; refusing a second entry." >&2
            return 1
        fi
        if [[ "$created" == 1 ]]; then
            echo "BOOT could not verify the newly registered firmware entry." >&2
            return 1
        fi
        created=1
        efibootmgr -c -d "$disk" -p "$partition" -L "BOOT Boot Manager" -l "$loader_path" || return 1
    done

    efibootmgr -b "$boot_number" -a || return 1
    order=$(efibootmgr | sed -n 's/^BootOrder:[[:space:]]*//p' | head -n 1)
    [[ -n "$order" ]] || return 1
    IFS=, read -r -a numbers <<< "$order"
    new_order="$boot_number"
    for item in "${numbers[@]}"; do
        [[ "$item" =~ ^[[:xdigit:]]{4}$ ]] || {
            echo "BOOT received malformed UEFI BootOrder." >&2
            return 1
        }
        if [[ "${item^^}" != "$boot_number" ]]; then
            new_order+=",$item"
        fi
    done
    efibootmgr -o "$new_order" || return 1
    verified=$(efibootmgr | sed -n 's/^BootOrder:[[:space:]]*//p' | head -n 1)
    [[ "${verified%%,*}" == "$boot_number" ]] || {
        echo "BOOT firmware did not retain BOOT as the default UEFI entry." >&2
        return 1
    }
    echo "BOOT generic package installed and registered Boot$boot_number."
}

# Installing a generic package is an ESP/NVRAM mutation, never build-only.
# Validate privilege, architecture, package, partition, file install and
# firmware registration in order; failure must terminate the chain.
boot_generic_main() {
    (($# == 0)) || { echo "BOOT generic installer accepts no arguments." >&2; return 2; }
    [[ $(id -u) == 0 ]] || { echo "BOOT generic installation requires root." >&2; return 1; }
    [[ $(uname -m) == x86_64 ]] || { echo "BOOT generic package requires x86-64." >&2; return 1; }
    local source=/usr/share/infiltrator-boot/boot
    boot_generic_payload_check "$source"
    boot_generic_esp
    boot_generic_install_files "$BOOT_GENERIC_ESP" "$source"
    boot_generic_register "$BOOT_GENERIC_DISK" "$BOOT_GENERIC_PART" "$BOOT_GENERIC_PARTUUID"
}
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    boot_generic_main "$@"
fi
