ISSUE DRAFTS
============

This folder contains draft GitHub issue bodies to help convert the ROADMAP 1.0 remaining items into issues quickly.

How to create issues from these drafts
------------------------------------
1. Install GitHub CLI (gh) and authenticate: `gh auth login`.
2. Run the provided PowerShell script `tools\create_github_issues.ps1` (from repo root) to create issues from the drafts.

Notes
-----
- Draft files are prefixed with numbers to preserve a suggested ordering. Each draft includes description, acceptance criteria, and suggested labels.
- The script will skip `README.md` and any files in this folder that don't begin with a '#' title.
