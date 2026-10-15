# Provenance and Licensing Context

## Purpose

This document records how InfiltratorOS Boot Manager began and preserves the source-history context required for attribution, licensing and technical archaeology.

It is deliberately separate from product design and architecture. Provenance explains where code came from; it does not define what the product is.

## Initial source basis

The initial InfiltratorOS Boot Manager source tree was imported from the rEFInd upstream `master` tree at commit:

`cf542da4459ff18535b0ae3a7910af032834e469`

That upstream commit was authored by Rod Smith on 7 January 2026.

The imported tree supplied an already-working EFI implementation from which InfiltratorOS development could begin. The project intentionally chose a proven starting implementation rather than pretending to be a clean-room or independently originated EFI boot manager.

That decision does not make rEFInd the ongoing product specification, design authority or architectural owner of InfiltratorOS Boot Manager. Existing mechanisms are retained, changed or replaced according to current InfiltratorOS requirements and the canonical Infiltrator Engineering Philosophy.

## Historical material retained in the repository

The repository retains upstream historical and legal material including:

- `README.txt`;
- `BUILDING.txt`;
- `NEWS.txt`;
- `CREDITS.txt`;
- `LICENSE.txt`;
- `COPYING.txt`; and
- licence files carried by relevant source or filesystem-driver subdirectories.

Those files remain useful for historical behaviour, original installation/build assumptions, credits and legal obligations. Maintained Infiltrator product documentation is authoritative for current design and engineering practice where historical documentation no longer reflects the project.

## Licensing

The repository contains code derived from rEFInd, rEFIt and other upstream components under multiple licences. rEFInd's retained licensing material describes GPLv3 coverage for modifications in the rEFInd lineage, the rEFIt licence for original rEFIt portions, and separate licensing for some filesystem-driver components. Historical rEFInd documentation has its own GNU FDL terms.

The authoritative legal text remains the actual licence files, not this summary. Nothing in Infiltrator product documentation removes, weakens or replaces upstream attribution or licence obligations.

## Attribution versus identity

The project keeps attribution precise because engineering provenance matters. That precision must not be confused with product identity.

Correct wording is:

- the repository **began from** a specific rEFInd source snapshot;
- some current source remains derived from or based on that material; and
- InfiltratorOS Boot Manager is the current product and owns its current requirements, architecture and direction.

The product should not be routinely described as though its historical source basis were its defining feature. References to rEFInd belong where they carry technical, historical or legal meaning.

## Migration principle

Historical file, directory and symbol names may remain while they are part of working technical contracts. Their presence is not a requirement to preserve historical architecture indefinitely.

When code is redesigned or replaced, required authorship and licensing information must remain accurate. When a historical mechanism is removed entirely, obsolete product-facing references to that mechanism should be removed as well rather than preserved as decorative history.
