# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in either demo, please use
GitHub's built-in [Report a Vulnerability][advisory] feature for a
private and secure disclosure.

When reporting, include:

- A clear description of the vulnerability.
- Steps to reproduce the issue.
- Potential impact of the vulnerability.

## Scope

These are demonstration programs for trade shows, exhibitions, and kiosk
displays.  Breeze embeds a web view and fetches weather over the
network, so it has an attack surface worth reporting on, but neither
demo is hardened for use on an untrusted network or as a security
boundary.  Run them on a display you control.

## Supported Versions

We provide fixes only for the main branch.

Individual support contracts are provided by _Wires_, see
<https://www.wires.se>.

## Acknowledgments

We appreciate the efforts of the security community.  Thank you for your
responsible disclosure.

[advisory]: https://github.com/kernelkit/demo/security/advisories/new
