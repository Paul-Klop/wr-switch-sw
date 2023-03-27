#ifndef WRS_VERSION_GROUP_H
#define WRS_VERSION_GROUP_H

#define WRSVERSION_OID WRS_OID, 6, 3

#define wrsVersionSwVersion_i			0
#define wrsVersionSwBuildBy_i			1
#define wrsVersionSwBuildDate_i			2
#define wrsVersionBackplaneVersion_i		3
#define wrsVersionFpgaType_i			4
#define wrsVersionManufacturer_i		5
#define wrsVersionSwitchSerialNumber_i		6
#define wrsVersionScbVersion_i			7
#define wrsVersionGwVersion_i			8
#define wrsVersionGwBuild_i			9
#define wrsVersionSwitchHdlCommitId_i		10
#define wrsVersionGeneralCoresCommitId_i	11
#define wrsVersionWrCoresCommitId_i		12
/* wrsVersionLastUpdateDate */
#define wrsVersionFeatures_i			13
#define wrsVersionSIZE				14


struct wrsVersion_s {
	char wrsVersions[wrsVersionSIZE][32];	/* array of version strings */
	char wrsVersionLastUpdateDate[32];
};

extern struct wrsVersion_s wrsVersion_s;
time_t wrsVersion_data_fill(void);

void init_wrsVersionGroup(void);
#endif /* WRS_VERSION_GROUP_H */
