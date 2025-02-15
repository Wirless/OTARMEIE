//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////


/*
=================================================================================================
MULTI-THREADED MAP RENDERING OPTIMIZATION TASK
=================================================================================================

OBJECTIVE:
Optimize the map rendering system in Remere's Map Editor for multi-threaded performance while 
maintaining OpenGL context safety and preventing race conditions.

KEY COMPONENTS AND TASKS:
------------------------

1. THREAD POOL MANAGEMENT
- Implement a dedicated MapRenderThreadPool class to manage worker threads
- Create thread-safe task queue for render segments
- Ensure proper thread lifecycle management and cleanup
- Handle OpenGL context sharing between threads
- Implement priority-based task scheduling

2. TILE RENDERING SEGMENTATION
- Divide visible map area into render segments (configurable size, default 96x96 tiles)
- Implement segment-based dirty region tracking
- Create efficient tile batching system for each segment
- Maintain render order for proper transparency handling
- Cache rendered segments for reuse

3. MEMORY MANAGEMENT AND SAFETY
- Implement thread-safe tile access mechanisms
- Create tile data caching system with read-write locks
- Optimize memory allocation patterns for multi-threaded access
- Implement memory pooling for frequently accessed tile data
- Ensure proper synchronization of shared resources

4. RENDER PIPELINE OPTIMIZATION
- Create segment-based render queue
- Implement priority system for visible segments
- Handle dynamic updates during scrolling/zooming
- Maintain OpenGL state consistency across threads
- Implement efficient texture batching

5. MINIMAP INTEGRATION
- Separate minimap rendering into dedicated thread
- Implement async minimap updates
- Create efficient data sharing between main render and minimap
- Optimize minimap memory usage
- Handle minimap cache invalidation

6. PERFORMANCE MONITORING
- Add performance metrics collection
- Track render times per segment
- Monitor thread utilization
- Implement debug visualization for segment rendering
- Create performance logging system

TECHNICAL CONSIDERATIONS:
------------------------
- OpenGL Context: Ensure proper context sharing and state management
- Memory Safety: Implement proper synchronization for shared resources
- Cache Coherency: Optimize data layout for cache-friendly access
- Thread Communication: Minimize inter-thread communication overhead
- State Management: Maintain consistent render state across threads

IMPLEMENTATION APPROACH:
-----------------------
1. Create base infrastructure (thread pool, task queue)
2. Implement segment management system
3. Add thread-safe memory management
4. Integrate with existing render pipeline
5. Optimize and tune performance
6. Add monitoring and debugging tools

PERFORMANCE TARGETS:
-------------------
- Maintain 60+ FPS for normal map viewing
- Support efficient rendering of large maps (8000x8000+ tiles)
- Minimize memory overhead from multi-threading
- Reduce CPU usage during idle states
- Optimize memory bandwidth usage

COMPATIBILITY REQUIREMENTS:
-------------------------
- Maintain compatibility with existing brush system
- Preserve all current rendering features
- Support all existing map operations
- Maintain backward compatibility with saved maps
- Support all current view options

=================================================================================================
*/

#ifndef RME_BASE_MAP_H_
#define RME_BASE_MAP_H_

#include "main.h"
#include "position.h"
#include "filehandle.h"
#include "map_allocator.h"
#include "tile.h"

// Class declarations
class QTreeNode;
class BaseMap;
class MapIterator;
class Floor;
class QTreeNode;
class TileLocation;

class MapIterator {
public:
	MapIterator(BaseMap* _map = nullptr);
	~MapIterator();
	MapIterator(const MapIterator& other);

	TileLocation* operator*();
	TileLocation* operator->();
	MapIterator& operator++();
	MapIterator operator++(int);
	bool operator==(const MapIterator& other) const {
		if (other.local_z != local_z) {
			return false;
		}
		if (other.local_i != local_i) {
			return false;
		}
		if (other.nodestack == nodestack) {
			return true;
		}
		if (other.current_tile == current_tile) {
			return true;
		}
		return false;
	}
	bool operator!=(const MapIterator& other) const {
		return !(other == *this);
	}

	struct NodeIndex {
		NodeIndex(QTreeNode* _node) :
			index(0), node(_node) { }
		NodeIndex(const NodeIndex& other) :
			index(other.index), node(other.node) { }
		int index;
		QTreeNode* node;

		bool operator==(const NodeIndex& n) const {
			return n.node == node && n.index == index;
		}
	};

private:
	std::vector<NodeIndex> nodestack;
	int local_i, local_z;
	TileLocation* current_tile;
	BaseMap* map;

	friend class BaseMap;
};

class BaseMap {
public:
	BaseMap();
	virtual ~BaseMap();

	// This doesn't destroy the map structure, just clears it, if param is true, delete all tiles too.
	void clear(bool del = true);
	MapIterator begin();
	MapIterator end();
	uint64_t size() const {
		return tilecount;
	}

	// these functions take a position and returns a tile on the map
	Tile* createTile(int x, int y, int z);
	Tile* getTile(int x, int y, int z);
	Tile* getTile(const Position& pos);
	Tile* getOrCreateTile(const Position& pos);
	const Tile* getTile(int x, int y, int z) const;
	const Tile* getTile(const Position& pos) const;
	TileLocation* getTileL(int x, int y, int z);
	TileLocation* getTileL(const Position& pos);
	TileLocation* createTileL(int x, int y, int z);
	TileLocation* createTileL(const Position& pos);
	const TileLocation* getTileL(int x, int y, int z) const;
	const TileLocation* getTileL(const Position& pos) const;

	// Get a Quad Tree Leaf from the map
	QTreeNode* getLeaf(int x, int y) {
		return root.getLeaf(x, y);
	}
	QTreeNode* createLeaf(int x, int y) {
		return root.getLeafForce(x, y);
	}

	// Assigns a tile, it might seem pointless to provide position, but it is not, as the passed tile may be nullptr
	void setTile(int _x, int _y, int _z, Tile* newtile, bool remove = false);
	void setTile(const Position& pos, Tile* newtile, bool remove = false) {
		setTile(pos.x, pos.y, pos.z, newtile, remove);
	}
	void setTile(Tile* newtile, bool remove = false) {
		setTile(newtile->getX(), newtile->getY(), newtile->getZ(), newtile, remove);
	}
	// Replaces a tile and returns the old one
	Tile* swapTile(int _x, int _y, int _z, Tile* newtile);
	Tile* swapTile(const Position& pos, Tile* newtile) {
		return swapTile(pos.x, pos.y, pos.z, newtile);
	}

	// Clears the visiblity according to the mask passed
	void clearVisible(uint32_t mask);

	uint64_t getTileCount() const {
		return tilecount;
	}

public:
	MapAllocator allocator;

protected:
	uint64_t tilecount;

	QTreeNode root; // The Quad Tree root

	friend class QTreeNode;
};

inline Tile* BaseMap::getTile(int x, int y, int z) {
	TileLocation* l = getTileL(x, y, z);
	return l ? l->get() : nullptr;
}

inline Tile* BaseMap::getTile(const Position& pos) {
	TileLocation* l = getTileL(pos);
	return l ? l->get() : nullptr;
}

inline const Tile* BaseMap::getTile(int x, int y, int z) const {
	const TileLocation* l = getTileL(x, y, z);
	return l ? l->get() : nullptr;
}

inline const Tile* BaseMap::getTile(const Position& pos) const {
	const TileLocation* l = getTileL(pos);
	return l ? l->get() : nullptr;
}

#endif
