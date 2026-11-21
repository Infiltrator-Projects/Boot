# Engineering Philosophy

BOOT follows the Infiltrator engineering direction expressed by [Libraries/Common](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/PHILOSOPHY.md): engineering excellence, reliability, evidence, disciplined reduction, technical depth, and an interface whose simplicity is earned by correct underlying engineering. It is the boot manager provided by InfiltratorOS **for the whole computer**, not a mechanism for excluding competing operating systems.

## Non-negotiable behaviours

1. **Universal discovery, no artificial exclusivity.** BOOT must recognise every supported OS/loader it can actually reach, whether the machine has two installations or twenty. Never reduce discovery to machine-specific examples.
2. **Preserve independent boot paths.** Making BOOT the preferred firmware entry must not erase or overwrite Windows Boot Manager, GRUB, third-party loaders or the standard removable-media fallback. Hiding an entry within BOOT's menu is not deleting the executable.
3. **One owned installation location.** The native installer operates under `/EFI/InfiltratorOS/BOOT` only, verifies ownership before replacing its own loader, and refuses unsafe alternative targets. Its install operation must be complete and unambiguous.
4. **Respect the firmware boundary.** UEFI filesystems, EFI applications, NVRAM and Secure Boot have distinct roles and failure modes. Valid detection is not equivalent to valid execution; an executable's existence is not a guarantee of bootability.
5. **Treat uncertainty as uncertainty.** Automated builds and virtual firmware prove specific contracts. Real-machine compatibility requires actual hardware evidence. Unsupported volumes and unverifiable security conditions must never be reported as tested or supported.
6. **One official appearance.** BOOT is an InfiltratorOS product with an intended unified identity, not a downloadable or user-selected theme collection. A cosmetic redesign must leave discovery, recovery and boot safety intact.
7. **Protect successful behaviour.** Changes to BOOT's filesystem drivers, EFI dispatch, installer and NVRAM handling require evidence that the existing direct-kernel and alternative firmware paths are preserved. Do not replace working internals merely to tidy their presentation.

## Documentation and source commentary

Technical documentation should meet a postgraduate engineering standard: contracts, invariants, ownership, preconditions, failure/recovery semantics, real validation and known gaps. Historical text must not be mistaken for current executable truth.

The present source has deliberately had earlier comments removed for a future fresh commentary pass. This document does not reinstate those comments or change executable files. Where runtime behaviour and a document differ, inspect and correct the evidence rather than assuming the prose is authoritative.
