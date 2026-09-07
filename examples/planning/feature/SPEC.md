# Feature objective: usable catalog slugs

Complete the existing C command `catalog-slug` by implementing `slugify` in
`slug.c`. Deliver a buildable source archive containing the integrated command,
not just a patch or a review. The existing CLI and public header are fixed.

Requirements:

- `normalization`: ASCII letters become lowercase; digits remain; runs of other
  bytes become one hyphen between words. Leading/trailing delimiters disappear.
  Empty or delimiter-only input gives the empty string. Non-ASCII bytes are
  delimiters; this feature does not claim Unicode transliteration.
- `bounds`: `slugify(input, output, capacity)` returns 0 and a terminated string
  on success. If the result does not fit, input is NULL, output is NULL, or
  capacity is zero, return -1. When output is non-NULL and capacity is positive,
  every error leaves output[0] equal to zero. Never write outside capacity.
- `cli`: the existing command builds with strict C99 warnings and supports
  multiple arguments and `--limit 1..64`. An oversized slug fails with exit 2.
  Existing command argument errors retain their behavior.

Use an implementation worker, a composition step that builds the existing
command with its output, and an independent executable check of the sealed final
source archive. `verify.sh` and `test_slug.c` are fixed acceptance material.
