#ifndef GENERATOR_H
#define GENERATOR_H

/*
 * ISO9660 image generator.
 *
 * This is a thin wrapper around genisoimage (or mkisofs) that produces an
 * ISO9660 image from a source directory. It is the Generator half of the
 * "ISO Wrapper" interface described in the project README; the Parser
 * half lives in parser.h.
 */

/* Optional feature flags for iso_generate(). */
#define ISO_GEN_ROCK_RIDGE 0x01  /* pass -r (Rock Ridge, rationalized)  */
#define ISO_GEN_JOLIET     0x02  /* pass -J (Joliet long/Unicode names) */

/*
 * Generate an ISO9660 image from `srcdir` into `out_iso`.
 *
 *   srcdir     - directory whose contents become the ISO root
 *   out_iso    - path to the output .iso file (will be created/overwritten)
 *   volume_id  - volume label; NULL or "" means "use the tool's default"
 *   flags      - bitwise OR of ISO_GEN_* flags; 0 for a plain ISO9660 image
 *
 * Returns 0 on success, -1 on failure (a diagnostic is printed to stderr).
 *
 * Implementation note: tries `genisoimage` first, then falls back to
 * `mkisofs` if the former is not on PATH.
 */
int iso_generate(const char *srcdir,
                 const char *out_iso,
                 const char *volume_id,
                 int flags);

#endif /* GENERATOR_H */
