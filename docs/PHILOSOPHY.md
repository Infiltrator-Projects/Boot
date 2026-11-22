# Engineering Philosophy

BOOT follows the Infiltrator engineering direction expressed by [Libraries/Common](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/PHILOSOPHY.md): engineering excellence, reliability, evidence, disciplined reduction, technical depth, and an interface whose simplicity is earned by correct underlying engineering. It is the boot manager provided by InfiltratorOS **for the whole computer**, not a mechanism for excluding competing operating systems.

## ELEGANCE — the design approach

**ELEGANCE is an artistic, visual quality standard**, not a demand for fewer words, fewer comments or fewer graphical elements. BOOT should convey the carefully composed sophistication associated with Mercedes-Benz, Fendi and Prada: beautiful proportion, impeccable alignment, purposeful typography, subtle contrast, elegant rhythm, exceptional finishing and an unmistakable identity. Those names are benchmarks for craft, not instructions to copy brand artwork.

In BOOT this means an art-directed firmware screen, thoughtfully placed icons, balanced typography, coherent official imagery, refined accents and a polished presentation across resolutions. A restrained design may be elegant, but minimalism by itself is not elegance; complexity and rich detail may also be elegant when composed exceptionally well. No cosmetic decision may impair boot selection or recovery.

**The same visual standard applies to source code and Markdown.** File introductions, function contracts and long technical explanations should be visually composed for a human reader: attractive hierarchy, consistent alignment, meaningful grouping and deliberately judged whitespace. Do not convert complete documentation into terse notes, replace expressive presentation with a generic template, or mistake oversized ASCII ornament and mechanical banners for luxury design. Preserve the full postgraduate-level explanation of algorithms, design decisions, invariants, ownership, errors, limitations and recovery while improving how that information looks.

**Acceptance is visual as well as technical.** Judge the actual screen, document or editor view as a complete composition rather than by comment counts, line counts, code-format metrics or the absence of decoration. The definitive family-wide interpretation is recorded in [InfiltratorOS Engineering Philosophy](https://github.com/Infiltrator-Projects/InfiltratorOS/blob/main/docs/PHILOSOPHY.md#elegance--the-artistic-design-authority).

## Non-negotiable behaviours

1. **Universal discovery, no artificial exclusivity.** BOOT must recognise every supported OS/loader it can actually reach, whether the machine has two installations or twenty. Never reduce discovery to machine-specific examples.
2. **Preserve independent boot paths.** Making BOOT the preferred firmware entry must not erase or overwrite Windows Boot Manager, GRUB, third-party loaders or the standard removable-media fallback. Hiding an entry within BOOT's menu is not deleting the executable.
3. **One owned installation location.** The native `.run` and generic `.deb` install exclusively beneath `/EFI/InfiltratorOS/BOOT`. Reinstallation intentionally replaces the complete reserved directory, including previous configuration, without an ownership marker; the installers must reject alternate targets and symbolic-link traversal. Its install operation must be complete and unambiguous.
4. **Respect the firmware boundary.** UEFI filesystems, EFI applications, NVRAM and Secure Boot have distinct roles and failure modes. Valid detection is not equivalent to valid execution; an executable's existence is not a guarantee of bootability.
5. **Treat uncertainty as uncertainty.** Automated builds and virtual firmware prove specific contracts. Real-machine compatibility requires actual hardware evidence. Unsupported volumes and unverifiable security conditions must never be reported as tested or supported.
6. **One supplied official identity.** BOOT ships the InfiltratorOS default artwork, not a library of alternative themes. Users retain the ability to select their own PNG/JPEG assets or no image assets through `boot.conf`. A cosmetic redesign must leave discovery, recovery and boot safety intact.
7. **Protect successful behaviour.** Changes to BOOT's filesystem drivers, EFI dispatch, installer and NVRAM handling require evidence that the existing direct-kernel and alternative firmware paths are preserved. Do not replace working internals merely to tidy their presentation.

## Documentation and source commentary

Technical documentation should meet a postgraduate engineering standard: contracts, invariants, ownership, preconditions, failure/recovery semantics, real validation and known gaps. Historical text must not be mistaken for current executable truth.

Maintained source files have undergone an initial commentary pass followed by a focused technical-contract review of firmware, UI, graphics and filesystem boundaries. The presence of those comments is not a quality certificate: each describes inspected implementation behaviour, while testable external claims require separate evidence. Generated bytes and third-party decoder bodies are not bulk-commented solely to inflate coverage.

## Source-comment editorial standard

The source itself must be a deliberately composed reading experience. BOOT-owned C and header files use a restrained `BOOT / Responsibility` heading, followed by clearly grouped prose; shell scripts use an equivalent `#` heading. A single strong title and well-judged paragraph rhythm replace fake wordmarks, decorative rules and crowded introductory blocks.

Function explanations retain their natural hierarchy: describe what the routine accomplishes, why a non-obvious choice exists, and the relevant invariants, preconditions, allocation ownership and failure outcomes. Where behaviour is difficult to infer, **concrete examples are part of the documentation**. Preserve useful historical firmware rationale from the original implementation instead of rewriting it into an abstract contract alone.

Use ordinary `/* ... */` blocks for module context and local engineering rationale, and preserve `/** ... */` where an API-level declaration is being documented. Spacing and wrapping should feel balanced in an actual source editor. These are typographic conventions, not a mandate to compress commentary or mechanically label every function. Avoid repetitive statements, filler headings, artificial blank regions and overly dense unbroken paragraphs.

Bundled third-party, generated and externally defined ABI material must retain its required structure and provenance; aesthetic reformatting does not justify rewriting their implementations or erasing their history. **Content fidelity and visual elegance are complementary requirements.** Review both the prose and its rendered arrangement before claiming the work finished.

## Postgraduate-quality comment contract

A comment earns its place by explaining a non-obvious engineering fact or decision. File introductions state responsibility, operating context and trust boundaries. Functions with meaningful resource or security semantics identify preconditions, output/side-effect contracts, owned versus borrowed memory, ordering constraints, failure behaviour and limitations. Algorithms describe units, invariants, overflow assumptions and the reason for nontrivial branches. Public interfaces distinguish detection, parsing, validation, authentication and execution.

Avoid commentary that repeats the function name, narrates each assignment, duplicates an adjacent paragraph, or makes a proposed architecture sound deployed. A serious uncorrected hazard is labelled as a *known issue*, not a claim that the code safely rejects it. A working design can be documented without asserting that every platform has been tested.

Generated byte arrays and third-party decoding implementations do not require ornamental line-by-line commentary. Their integration points, provenance, interface assumptions, memory limits and data trust boundaries do require explanation. Source-level detail and architectural documents should complement rather than reproduce each other.

## Engineering evidence and change discipline

Separate five categories: **required behaviour**, **implementation present**, **test executed**, **hardware observation** and **unverified objective**. A compiler invocation does not demonstrate a successful loader launch; an OVMF menu does not demonstrate a successful OS boot; a filesystem magic check does not demonstrate a safe mount.

Where documentation and current code conflict, the code and executable fixtures describe current behaviour. Record the contradiction explicitly until resolved. A security or memory-safety concern deserves a location, failure mechanism and a regression requirement; polished prose alone is not remediation.

For a comment-only assignment, use a code-preservation comparison against the exact base commit. Correct inaccurate prose and declaration placement but do not treat a newly observed defect as permission to change the firmware implementation, build scripts, tests or installers. Source equivalence and runtime validation are separate evidence categories.
