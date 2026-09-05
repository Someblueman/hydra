# Rolling release checklist

Use this checklist for each post-2.0 release. Record evidence in the release notes
or PR rather than creating another version-specific checklist file.

## Prepare the change

- Define one useful scope and its acceptance boundaries; defer unrelated backlog.
- Choose patch, minor, or major from the public compatibility impact described in
  [VERSIONING.md](VERSIONING.md).
- Finalize the changelog and release notes; update CLI and native version strings
  together. Ensure help, completions, contracts, and migration instructions agree.
- Update the README installation tag and review its examples against the release.
  Refresh the [demo source and transcript](../assets/demos/README.md) when the
  demonstrated behavior changes; keep recordings explicit about their version.
- Remove delivered items from the roadmap, retaining unfinished boundaries.
- Run the checks appropriate to the change before requesting review.

## Qualify the merged candidate

Record the full merged commit SHA and selected version. Work from a clean checkout
of that commit; preserve unrelated files in existing development checkouts.

```sh
git rev-parse HEAD
git status --short
make test-all
make sanitize
git diff --check HEAD^
```

- Require all applicable hosted Ubuntu/macOS checks on that exact SHA.
- Record one real end-to-end scenario with commands and observed results.
- Check applicable failure, interruption, recovery, trust, and migration behavior.
- Verify shell-only install and optional native install/parity. For changed durable
  formats, include migration and rollback from the preceding stable release.
- Build source archives from the qualified Git commit, not the working directory.
  Generate SHA-256 checksums for every attached archive. If publishing optional
  native artifacts, build each advertised platform at the same SHA and preserve
  its source, platform, dependency, and checksum metadata.

## Publish and verify

- Confirm publication authorization and that the version tag does not exist.
- Create the version tag at the qualified SHA; never move an existing release tag.
- Create a draft GitHub release referencing that tag and attach all assets and
  checksums before publishing, including when release immutability is enabled.
- Inspect the draft notes, version, prerelease status, and asset inventory.
- Publish the draft only after qualification and asset preparation are complete.
- Verify the remote tag dereferences to the recorded SHA, the hosted release uses
  that tag, and downloaded assets match the checksum manifest.
- Report qualification, merge, tag, and hosted publication as separate outcomes.

If qualification fails, fix and qualify a new candidate. If an already published
release is defective, issue a new version; do not overwrite published artifacts.

GitHub documents [drafts and immutable release preparation](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)
and [enabling release immutability](https://docs.github.com/en/code-security/how-tos/secure-your-supply-chain/establish-provenance-and-integrity/prevent-release-changes).
