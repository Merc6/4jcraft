#pragma once

#include <stdint.h>

#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "CompressedTileStorage.h"
#include "SparseDataStorage.h"
#include "SparseLightStorage.h"
#include "minecraft/world/entity/Entity.h"
#include "minecraft/world/level/Level.h"
#include "minecraft/world/level/LightLayer.h"
#include "minecraft/world/level/TilePos.h"
#include "minecraft/world/level/chunk/storage/palette_storage.hh"

class DataLayer;
class TileEntity;
class Random;
class ChunkSource;
class EntitySelector;
class AABB;
class Biome;
class BiomeSource;
class ChunkPos;
class CompoundTag;
class CompressedTileStorage;
class DataInputStream;
class DataOutputStream;
class Entity;
class SparseDataStorage;
class SparseLightStorage;

#define SHARING_ENABLED
class TileCompressData_SPU;

class LevelChunk {
    friend class TileCompressData_SPU;
    friend class LevelRenderer;

protected:
    // 4J Stu - Stopped this being private so we can add some more logic to it
    bool m_unsaved;

public:
    static bool touchedSky;

    static constexpr int BLOCKS_LENGTH = Level::CHUNK_TILE_COUNT;  // 4J added
    static constexpr int sTerrainPopulatedFromHere = 2;
    static constexpr int sTerrainPopulatedFromW = 4;
    static constexpr int sTerrainPopulatedFromS = 8;
    static constexpr int sTerrainPopulatedFromSW = 16;

    // All the post-processing that can actually place tiles in this
    // chunk are complete
    static constexpr int sTerrainPopulatedAllAffecting = 30;
    static constexpr int sTerrainPopulatedFromNW = 32;
    static constexpr int sTerrainPopulatedFromN = 64;
    static constexpr int sTerrainPopulatedFromNE = 128;
    static constexpr int sTerrainPopulatedFromE = 256;
    static constexpr int sTerrainPopulatedFromSE = 512;

    // The post-processing passes of all neighbours to this chunk are complete
    static constexpr int sTerrainPopulatedAllNeighbours = 1022;

    // This chunk has been post-post-processed, which is only done when all
    // neighbours have been post-processed
    static constexpr int sTerrainPostPostProcessed = 1024;

    const int ENTITY_BLOCKS_LENGTH;

    // TODO fuck around with this value
    compression::PaletteVec<4, uint8_t> biomes{};  // 4J Stu - Made public
    compression::PaletteVec<4, uint8_t> heightmap{};

    int minHeight;
    int x, z;

    Level* level;
    std::unordered_map<TilePos, std::shared_ptr<TileEntity>, TilePosKeyHash,
                       TilePosKeyEq>
        tileEntities;
    std::vector<std::shared_ptr<Entity> >** entityBlocks;

    short terrainPopulated;  // 4J - changed from bool to bitfield within short

    bool dontSave;
    bool lastSaveHadEntities;
#if defined(SHARING_ENABLED)
    bool sharingTilesAndData;  // 4J added
#endif
    bool emissiveAdded;        // 4J added
    int64_t lastUnsharedTime;  // 4J added
    int64_t lastSaveTime;
    bool seenByPlayer;
    int lowestHeightmap;
    int64_t inhabitedTime;

    bool loaded;
#if defined(_LARGE_WORLDS)
    CompoundTag* m_unloadedEntitiesTag;
#endif

    enum EColumnFlag {
        eColumnFlag_recheck = 1,
        eColumnFlag_biomeOk = 2,
        eColumnFlag_biomeHasSnow = 4,
        eColumnFlag_biomeHasRain = 8,
    };

    static void reorderBlocksAndDataToXZY(int y0, int xs, int ys, int zs,
                                          std::vector<uint8_t>* data);

    // Set block data to that passed in in the input array of size 32768
    void setBlockData(std::vector<uint8_t>& data);

    // Sets data in passed in array of size 32768, from the block data in this
    // chunk
    void getBlockData(std::vector<uint8_t>& data);

    int getBlocksAllocatedSize(int* count0, int* count1, int* count2,
                               int* count4, int* count8);

    // Set data to that passed in in the
    // input array of size 32768
    void setDataData(std::vector<uint8_t>& data);

    // Sets data in passed in array of size
    // 16384, from the data in this chunk
    void getDataData(std::vector<uint8_t>& data);

    // Get a byte array of length 16384 ( 128 x 16 x 16 x 0.5 ),
    // containing sky light data. Ordering same as java version.
    void getSkyLightData(std::vector<uint8_t>& data);

    // Get a byte array of length 16384 ( 128 x 16 x 16 x
    // 0.5 ), containing block light data. Ordering same
    // as java version.
    void getBlockLightData(std::vector<uint8_t>& data);

    // Set sky light data to data passed in input byte
    // array of length 16384. This data must be in
    // original (java version) order
    void setSkyLightData(std::vector<uint8_t>& data);

    // Set block light data to data passed in input byte
    // array of length 16384. This data must be in
    // original (java version) order
    void setBlockLightData(std::vector<uint8_t>& data);

    // Set sky light data to be all fully lit
    void setSkyLightDataAllBright();

    bool isLowerBlockStorageCompressed();
    int isLowerBlockLightStorageCompressed();
    int isLowerDataStorageCompressed();
    bool isRenderChunkEmpty(int y);
    void writeCompressedBlockData(DataOutputStream* dos);
    void writeCompressedDataData(DataOutputStream* dos);
    void writeCompressedSkyLightData(DataOutputStream* dos);
    void writeCompressedBlockLightData(DataOutputStream* dos);
    void readCompressedBlockData(DataInputStream* dis);
    void readCompressedDataData(DataInputStream* dis);
    void readCompressedSkyLightData(DataInputStream* dis);
    void readCompressedBlockLightData(DataInputStream* dis);
    void compressLighting();  // 4J added
    void compressBlocks();    // 4J added
    void compressData();      // 4J added
    int getHighestNonEmptyY();
    std::vector<uint8_t> getReorderedBlocksAndData(int x, int y, int z, int xs,
                                                   int& ys, int zs);
    void setUnsaved(bool unsaved);          // 4J added
    void recheckGaps(bool bForce = false);  // 4J - added parameter, made public
    void stopSharingTilesAndData();         // 4J added
    void startSharingTilesAndData(int forceMs = 0);  // 4J added
    int getHighestSectionPosition();

    LevelChunk(Level* level, int x, int z);
    LevelChunk(Level* level, std::vector<uint8_t>& blocks, int x, int z);
    LevelChunk(Level* level, int x, int z, LevelChunk* lc);
    virtual ~LevelChunk();

    virtual void reSyncLighting();  // 4J added
    virtual void init(Level* level, int x, int z);
    virtual bool isAt(int x, int z);
    virtual int getHeightmap(int x, int z);
    virtual void recalcBlockLights();
    virtual void recalcHeightmapOnly();
    virtual void recalcHeightmap();
    virtual void lightLava();
    virtual int getTileLightBlock(int x, int y, int z);
    virtual int getTile(int x, int y, int z);
    virtual bool setTileAndData(int x, int y, int z, int _tile, int _data);
    virtual bool setTile(int x, int y, int z, int _tile);
    virtual int getData(int x, int y, int z);
    virtual bool setData(int x, int y, int z, int val, int mask,
                         bool* maskedBitsChanged);  // 4J added mask
    virtual int getBrightness(LightLayer::variety layer, int x, int y, int z);
    virtual void getNeighbourBrightnesses(int* brightnesses,
                                          LightLayer::variety layer, int x,
                                          int y, int z);  // 4J added
    virtual void setBrightness(LightLayer::variety layer, int x, int y, int z,
                               int brightness);
    virtual int getRawBrightness(int x, int y, int z, int skyDampen);
    virtual void addEntity(std::shared_ptr<Entity> e);
    virtual void removeEntity(std::shared_ptr<Entity> e);
    virtual void removeEntity(std::shared_ptr<Entity> e, int yc);
    virtual bool isSkyLit(int x, int y, int z);
    virtual void skyBrightnessChanged();
    virtual std::shared_ptr<TileEntity> getTileEntity(int x, int y, int z);
    virtual void addTileEntity(std::shared_ptr<TileEntity> te);
    virtual void setTileEntity(int x, int y, int z,
                               std::shared_ptr<TileEntity> tileEntity);
    virtual void removeTileEntity(int x, int y, int z);
    virtual void load();
    virtual void unload(bool unloadTileEntities);  // 4J - added parameter
    virtual bool containsPlayer();                 // 4J - added
#if defined(_LARGE_WORLDS)
    virtual bool isUnloaded();
#endif

#if defined(LIGHT_COMPRESSION_STATS)
    int getBlockLightPlanesLower() { return lowerBlockLight->count; }
    int getSkyLightPlanesLower() { return lowerSkyLight->count; }
    int getBlockLightPlanesUpper() { return upperBlockLight->count; }
    int getSkyLightPlanesUpper() { return upperSkyLight->count; }
#endif

#if defined(DATA_COMPRESSION_STATS)
    int getDataPlanes() { return data->count; }
#endif

    virtual void markUnsaved();
    virtual void getEntities(std::shared_ptr<Entity> except, AABB* bb,
                             std::vector<std::shared_ptr<Entity> >& es,
                             const EntitySelector* selector);
    virtual void getEntitiesOfClass(const std::type_info& ec, AABB* bb,
                                    std::vector<std::shared_ptr<Entity> >& es,
                                    const EntitySelector* selector);
    virtual int countEntities();
    virtual bool shouldSave(bool force);
    virtual int getBlocksAndData(
        std::vector<uint8_t>* data, int x0, int y0, int z0, int x1, int y1,
        int z1, int p,
        bool includeLighting = true);  // 4J - added includeLighting parameter
    static void tileUpdatedCallback(int x, int y, int z, void* param,
                                    int yparam);  // 4J added
    virtual int setBlocksAndData(
        std::vector<uint8_t>& data, int x0, int y0, int z0, int x1, int y1,
        int z1, int p,
        bool includeLighting = true);  // 4J - added includeLighting parameter
    virtual bool testSetBlocksAndData(std::vector<uint8_t>& data, int x0,
                                      int y0, int z0, int x1, int y1, int z1,
                                      int p);  // 4J added
    virtual Random* getRandom(int64_t l);
    virtual bool isEmpty();
    virtual void attemptCompression();

    static void staticCtor();
    void checkPostProcess(ChunkSource* source, ChunkSource* parent, int x,
                          int z);
    void checkChests(ChunkSource* source, int x, int z);  // 4J added
    int getTopRainBlock(int x,
                        int z);  // 4J - optimisation brought forward from 1.8.2
    void tick();  // 4J - lighting change brought forward from 1.8.2
    ChunkPos* getPos();
    bool isYSpaceEmpty(int y1, int y2);
    void reloadBiomes();  // 4J added
    virtual Biome* getBiome(int x, int z, BiomeSource* biomeSource);
    std::vector<uint8_t> getBiomes();
    void setBiomes(std::vector<uint8_t>& biomes);
    bool biomeHasRain(int x, int z);      // 4J added
    bool biomeHasSnow(int x, int z);      // 4J added
    void updateBiomeFlags(int x, int z);  // 4J added

#if defined(SHARING_ENABLED)
    static std::recursive_mutex m_csSharing;  // 4J added
#endif
    // 4J  added
    static std::recursive_mutex m_csEntities;
    static std::recursive_mutex m_csTileEntities;  // 4J  added

private:
    mutable std::mutex m_biomes_mutex;

    // 4J - actual storage for blocks is now private with public methods to
    // access it
    CompressedTileStorage* lowerBlocks;  // 0 - 127
    CompressedTileStorage* upperBlocks;  // 128 - 255

    // 4J - actual storage for data is now private with public methods to access
    // it
    SparseDataStorage* lowerData;  // 0 - 127
    SparseDataStorage* upperData;  // 128 - 255

    // 4J - actual storage for sky & block lights is now private with new
    // methods to be able to access it.

    std::unique_ptr<SparseLightStorage> lowerSkyLight;    // 0 - 127
    std::unique_ptr<SparseLightStorage> upperSkyLight;    // 128 - 255
    std::unique_ptr<SparseLightStorage> lowerBlockLight;  // 0 - 127
    std::unique_ptr<SparseLightStorage> upperBlockLight;  // 128 - 255

    bool hasGapsToCheck;

    // 4J - optimisation brought forward from 1.8.2
    // (was int arrayb in java though)
    std::array<uint8_t, 256> rainHeights;

    // 4J - lighting update brought forward
    // from 1.8.2, was a bool array but now
    // mixed with other flags in our
    // version, and stored in nybbles
    std::array<uint8_t, 128> columnFlags;

    short* serverTerrainPopulated;  // 4J added

    void lightGaps(int x, int z);
    void lightGap(int x, int z, int source);
    void lightGap(int x, int z, int y1, int y2);
    void recalcHeight(int x, int yStart, int z);
};
