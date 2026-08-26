# Security Policy

## Supported versions

Security fixes are made against the latest published release or release
candidate. Older builds may be asked to update before a report is investigated.

## Reporting a vulnerability

Report suspected vulnerabilities through
[GitHub private vulnerability reporting](https://github.com/TensorHarmony/radmarky-viewer/security/advisories/new).
Do not open a public issue for an unpatched vulnerability.

Include the affected version, impact, prerequisites, and minimal reproduction
steps. Do not submit patient-identifiable medical data, protected health
information, clinical binaries, or real patient archives. Use synthetic or
properly de-identified inputs.

Relevant reports include unsafe handling of untrusted image or archive inputs,
unexpected code execution, path traversal, and bypasses of security-sensitive
validation. Python annotation validators intentionally run as separate
processes with the current user's permissions. Installing and running an
untrusted validator is therefore outside the vulnerability model, but a bypass
of the application's explicit validator trust or execution controls is in
scope.

The maintainer will coordinate disclosure and a fix before public discussion
when the report is accepted.
