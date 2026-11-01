#pragma once
// Authoritative item/container model (SYS-03) with the SYS-12 invariants:
// every item is in exactly one existing container, in bounds, never overlapping.
// Every mutating call validates completely before it mutates anything.
#include "Types.h"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Scavar
{
enum class ContainerKind : uint8_t { Pockets = 0, Vest, Backpack, Secure, Stash, Returns, World, Corpse, Ground };
const char* ContainerKindName(ContainerKind kind);
bool ParseContainerKind(const std::string& name, ContainerKind* out);

enum class Status : uint8_t
{
    Ok = 0, NoSuchItem, NoSuchContainer, NoSuchDef, OutOfBounds, Overlap, StackLimit, WrongDef,
    WrongSource, WrongKind, InvalidArgument, NoSpace, AlreadyExists, NotEmpty
};
const char* StatusName(Status s);

struct ItemDef
{
    std::string Id;
    int W = 1, H = 1;
    float Weight = 0;
    int StackMax = 1;
};

class ItemCatalog
{
public:
    // Returns false for duplicate ids or non-positive dimensions / stack size.
    bool Register(const ItemDef& def);
    const ItemDef* Find(const std::string& id) const;
private:
    std::map<std::string, ItemDef> Defs;
};

struct ItemInstance
{
    ItemId Id = InvalidId;
    std::string DefId;
    int Qty = 1;
    float Durability = 100.f;
    ContainerId Container = InvalidId;
    int X = 0, Y = 0;
    bool Rot = false;
    uint32_t Flags = 0;
    int32_t Aux = 0;   // type-specific payload; magazines: 1 + index of the loaded ammo type in GameData.Ammo (0 = empty)
    bool operator==(const ItemInstance&) const = default;
};

struct Container
{
    ContainerId Id = InvalidId;
    ContainerKind Kind = ContainerKind::Ground;
    int W = 1, H = 1;
    int64_t Owner = 0;
    bool operator==(const Container&) const = default;
};

struct Placement { int X = 0, Y = 0; bool Rot = false; };

struct ItemChange { ItemId Id = InvalidId; std::optional<ItemInstance> After; };          // nullopt = deleted
struct ContainerChange { ContainerId Id = InvalidId; std::optional<Container> After; };   // nullopt = deleted
struct ChangeSet
{
    std::vector<ContainerChange> Containers;
    std::vector<ItemChange> Items;
    bool Empty() const { return Containers.empty() && Items.empty(); }
};

class ItemStore
{
public:
    explicit ItemStore(const ItemCatalog* catalog) : Catalog(catalog) {}

    // ---- containers
    Status AddContainer(Container c, ContainerId* outId = nullptr);   // c.Id == 0 => allocate
    Status RemoveContainer(ContainerId id);                           // must be empty
    Status SetContainerKind(ContainerId id, ContainerKind kind);

    // ---- items
    Status Create(const std::string& defId, int qty, ContainerId container, int x, int y, bool rot, ItemId* outId = nullptr, float durability = 100.f);
    // expectedSrc != 0 makes the move conditional on the item still being there (double-pickup guard).
    Status Move(ItemId id, ContainerId dst, int x, int y, bool rot, ContainerId expectedSrc = InvalidId);
    Status AutoMove(ItemId id, ContainerId dst, bool allowRotate, ContainerId expectedSrc = InvalidId);
    Status Merge(ItemId src, ItemId dst, int qty = 0);                // qty <= 0 => all of src
    Status Split(ItemId src, int qty, ContainerId dst, int x, int y, bool rot, ItemId* outId = nullptr);
    Status Remove(ItemId id);
    Status AutoPlace(const std::string& defId, int qty, ContainerId container, bool allowRotate, ItemId* outId = nullptr, float durability = 100.f);
    Status SetDurability(ItemId id, float durability);
    Status SetFlags(ItemId id, uint32_t flags);
    Status SetAux(ItemId id, int32_t aux);
    Status SetQty(ItemId id, int qty);                                // 1..StackMax; use Remove() to delete
    Status FindFreeSlot(const std::string& defId, ContainerId container, bool allowRotate, ItemId ignoreItem, Placement* out) const;

    // Atomic death resolution (AT-036): secure-pocket items go to the stash `returns` container, the body containers
    // become Corpse containers (contents intact) and the now-empty secure container is deleted. All or nothing.
    Status ResolveDeath(ContainerId secure, ContainerId returns, const std::vector<ContainerId>& bodyContainers);

    // ---- transactions (nestable; Rollback undoes everything since the matching BeginTx)
    void BeginTx();
    Status Commit();
    Status Rollback();
    bool InTx() const { return !Marks.empty(); }

    // ---- change tracking for persistence (not available while a tx is open: returns empty, keeps dirty set)
    ChangeSet DrainChanges();
    bool HasPendingChanges() const { return !DirtyItems.empty() || !DirtyContainers.empty(); }

    // ---- bulk import (used by Db::LoadInto): bypasses placement validation and change tracking
    void Clear();
    Status ImportContainer(const Container& c);
    Status ImportItem(const ItemInstance& item);
    void SetNextIds(ItemId nextItem, ContainerId nextContainer);

    // ---- queries
    const ItemCatalog* GetCatalog() const { return Catalog; }
    const ItemInstance* GetItem(ItemId id) const;
    const Container* GetContainer(ContainerId id) const;
    const std::vector<ItemId>& ItemsIn(ContainerId id) const;
    const std::map<ItemId, ItemInstance>& AllItems() const { return Items; }
    const std::map<ContainerId, Container>& AllContainers() const { return Containers; }
    float ContainerWeight(ContainerId id) const;
    std::map<std::string, int64_t> TotalQtyByDef() const;
    bool CheckInvariants(std::string* why) const;
    bool Equals(const ItemStore& other) const { return Items == other.Items && Containers == other.Containers; }
    ItemId NextItemId() const { return NextItem; }
    ContainerId NextContainerId() const { return NextContainer; }

private:
    struct Undo
    {
        bool IsContainer = false;
        int64_t Id = 0;
        std::optional<ItemInstance> Item;
        std::optional<Container> Cont;
    };

    bool Footprint(const std::string& defId, bool rot, int* w, int* h) const;
    Status ValidatePlacement(const Container& c, int w, int h, int x, int y, ItemId ignore) const;

    void RawPutItem(const ItemInstance& item);
    void RawEraseItem(ItemId id);
    void RawPutContainer(const Container& c);
    void RawEraseContainer(ContainerId id);
    void IndexInsert(ContainerId c, ItemId id);
    void IndexRemove(ContainerId c, ItemId id);
    void PutItem(const ItemInstance& item);
    void EraseItem(ItemId id);
    void PutContainer(const Container& c);
    void EraseContainer(ContainerId id);

    const ItemCatalog* Catalog;
    std::map<ItemId, ItemInstance> Items;
    std::map<ContainerId, Container> Containers;
    std::map<ContainerId, std::vector<ItemId>> Index;   // sorted ids per container
    ItemId NextItem = 1;
    ContainerId NextContainer = 1;
    std::vector<Undo> Log;
    std::vector<size_t> Marks;
    std::set<ItemId> DirtyItems;
    std::set<ContainerId> DirtyContainers;
};
} // namespace Scavar
