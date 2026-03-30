#ifndef SYSTEMS_H
#define SYSTEMS_H

/* systems.h — NextUI system tag mappings for ScreenScraper.fr and Libretro.
 *
 * Source of truth for system tags: NextUI core tags plus community pak tags
 * from the NextUI Pak Store. */

/* Look up a ScreenScraper.fr platform ID by NextUI system tag.
 * Returns the platform ID, or -1 if the tag is unknown. */
int ss_platform_id(const char *tag);

/* Look up the libretro-database cht/ directory name for a NextUI system tag.
 * Returns NULL if the system has no cheat database in libretro. */
const char *libretro_dir(const char *tag);

/* Look up a human-readable display name for a NextUI system tag.
 * Returns the tag itself if no display name is configured. */
const char *ss_display_name(const char *tag);

#endif /* SYSTEMS_H */
