#include "Inventory.h"
#include <algorithm>

namespace Scavar
{
const char* ContainerKindName(ContainerKind k)
{
    switch (k)
    {
    case ContainerKind::Pockets: return "pockets";
    case ContainerKind::Vest: return "vest";
    case ContainerKind::Backpack: return "backpack";
    case ContainerKind::Secure: return "secure";
    case ContainerKind::Stash: return "stash";
    case ContainerKind::Returns: return "returns";
    case ContainerKind::World: return "world";
    case ContainerKind::Corpse: return "corpse";
    case ContainerKind::Ground: return "ground";
    }
    return "ground";
}

bool ParseContainerKind(const std::string& s, ContainerKind* out)
{
    for (int i = 0; i <= static_cast<int>(ContainerKind::Ground); ++i)
        if (s == ContainerKindName(static_cast<ContainerKind>(i))) { *out = static_cast<ContainerKind>(i); return true; }
    return false;
}

const char* StatusName(Status s)
{
    switch (s)
    {
    case Status::Ok: return "Ok";
    case Status::NoSuchItem: return "NoSuchItem";
    case Status::NoSuchContainer: return "NoSuchContainer";
    case Status::NoSuchDef: return "NoSuchDef";
    case Status::OutOfBounds: return "OutOfBounds";
    case Status::Overlap: return "Overlap";
    case Status::StackLimit: return "StackLimit";
    case Status::WrongDef: return "WrongDef";
    case Status::WrongSource: return "WrongSource";
    case Status::WrongKind: return "WrongKind";
    case Status::InvalidArgument: return "InvalidArgument";
    case Status::NoSpace: return "NoSpace";
    case Status::AlreadyExists: return "AlreadyExists";
    case Status::NotEmpty: return "NotEmpty";
    }
    return "?";
}

bool ItemCatalog::Register(const ItemDef& def)
{
    if (def.Id.empty() || def.W < 1 || def.H < 1 || def.StackMax < 1) return false;
    return Defs.emplace(def.Id, def).second;
}
const ItemDef* ItemCatalog::Find(const std::string& id) const
{
    auto it = Defs.find(id);
    return it == Defs.end() ? nullptr : &it->second;
}

// ------------------------------------------------------------------ internals
bool ItemStore::Footprint(const std::string& defId, bool rot, int* w, int* h) const
{
    const ItemDef* d = Catalog ? Catalog->Find(defId) : nullptr;
    if (!d) return false;
    *w = rot ? d->H : d->W;
    *h = rot ? d->W : d->H;
    return true;
}

Status ItemStore::ValidatePlacement(const Container& c, int w, int h, int x, int y, ItemId ignore) const
{
    if (x < 0 || y < 0 || x + w > c.W || y + h > c.H) return Status::OutOfBounds;
    for (ItemId other : ItemsIn(c.Id))
    {
        if (other == ignore) continue;
        const ItemInstance& o = Items.at(other);
        int ow = 0, oh = 0;
        if (!Footprint(o.DefId, o.Rot, &ow, &oh)) continue;
        if (x < o.X + ow && o.X < x + w && y < o.Y + oh && o.Y < y + h) return Status::Overlap;
    }
    return Status::Ok;
}

void ItemStore::IndexInsert(ContainerId c, ItemId id)
{
    auto& v = Index[c];
    v.insert(std::lower_bound(v.begin(), v.end(), id), id);
}
void ItemStore::IndexRemove(ContainerId c, ItemId id)
{
    auto it = Index.find(c);
    if (it == Index.end()) return;
    auto& v = it->second;
    auto p = std::lower_bound(v.begin(), v.end(), id);
    if (p != v.end() && *p == id) v.erase(p);
}

void ItemStore::RawPutItem(const ItemInstance& item)
{
    auto it = Items.find(item.Id);
    if (it == Items.end()) IndexInsert(item.Container, item.Id);
    else if (it->second.Container != item.Container) { IndexRemove(it->second.Container, item.Id); IndexInsert(item.Container, item.Id); }
    Items[item.Id] = item;
}
void ItemStore::RawEraseItem(ItemId id)
{
    auto it = Items.find(id);
    if (it == Items.end()) return;
    IndexRemove(it->second.Container, id);
    Items.erase(it);
}
void ItemStore::RawPutContainer(const Container& c) { Containers[c.Id] = c; Index[c.Id]; }
void ItemStore::RawEraseContainer(ContainerId id) { Containers.erase(id); Index.erase(id); }

void ItemStore::PutItem(const ItemInstance& item)
{
    if (InTx())
    {
        Undo u; u.Id = item.Id;
        auto it = Items.find(item.Id);
        if (it != Items.end()) u.Item = it->second;
        Log.push_back(std::move(u));
    }
    DirtyItems.insert(item.Id);
    RawPutItem(item);
}
void ItemStore::EraseItem(ItemId id)
{
    if (InTx())
    {
        Undo u; u.Id = id;
        auto it = Items.find(id);
        if (it != Items.end()) u.Item = it->second;
        Log.push_back(std::move(u));
    }
    DirtyItems.insert(id);
    RawEraseItem(id);
}
void ItemStore::PutContainer(const Container& c)
{
    if (InTx())
    {
        Undo u; u.IsContainer = true; u.Id = c.Id;
        auto it = Containers.find(c.Id);
        if (it != Containers.end()) u.Cont = it->second;
        Log.push_back(std::move(u));
    }
    DirtyContainers.insert(c.Id);
    RawPutContainer(c);
}
void ItemStore::EraseContainer(ContainerId id)
{
    if (InTx())
    {
        Undo u; u.IsContainer = true; u.Id = id;
        auto it = Containers.find(id);
        if (it != Containers.end()) u.Cont = it->second;
        Log.push_back(std::move(u));
    }
    DirtyContainers.insert(id);
    RawEraseContainer(id);
}

// ------------------------------------------------------------------ containers
Status ItemStore::AddContainer(Container c, ContainerId* outId)
{
    if (c.W < 1 || c.H < 1) return Status::InvalidArgument;
    if (c.Id == InvalidId) c.Id = NextContainer;
    if (Containers.count(c.Id)) return Status::AlreadyExists;
    NextContainer = std::max(NextContainer, c.Id + 1);
    PutContainer(c);
    if (outId) *outId = c.Id;
    return Status::Ok;
}

Status ItemStore::RemoveContainer(ContainerId id)
{
    if (!Containers.count(id)) return Status::NoSuchContainer;
    if (!ItemsIn(id).empty()) return Status::NotEmpty;
    EraseContainer(id);
    return Status::Ok;
}

Status ItemStore::SetContainerKind(ContainerId id, ContainerKind kind)
{
    auto it = Containers.find(id);
    if (it == Containers.end()) return Status::NoSuchContainer;
    Container c = it->second;
    c.Kind = kind;
    PutContainer(c);
    return Status::Ok;
}

// ------------------------------------------------------------------ items
Status ItemStore::Create(const std::string& defId, int qty, ContainerId cid, int x, int y, bool rot, ItemId* outId, float durability)
{
    const ItemDef* d = Catalog ? Catalog->Find(defId) : nullptr;
    if (!d) return Status::NoSuchDef;
    if (qty < 1) return Status::InvalidArgument;
    if (qty > d->StackMax) return Status::StackLimit;
    auto cit = Containers.find(cid);
    if (cit == Containers.end()) return Status::NoSuchContainer;
    int w = 0, h = 0;
    Footprint(defId, rot, &w, &h);
    if (Status s = ValidatePlacement(cit->second, w, h, x, y, InvalidId); s != Status::Ok) return s;

    ItemInstance item;
    item.Id = NextItem++;
    item.DefId = defId; item.Qty = qty; item.Durability = durability;
    item.Container = cid; item.X = x; item.Y = y; item.Rot = rot;
    PutItem(item);
    if (outId) *outId = item.Id;
    return Status::Ok;
}

Status ItemStore::Move(ItemId id, ContainerId dst, int x, int y, bool rot, ContainerId expectedSrc)
{
    auto iit = Items.find(id);
    if (iit == Items.end()) return Status::NoSuchItem;
    if (expectedSrc != InvalidId && iit->second.Container != expectedSrc) return Status::WrongSource;
    auto cit = Containers.find(dst);
    if (cit == Containers.end()) return Status::NoSuchContainer;
    int w = 0, h = 0;
    if (!Footprint(iit->second.DefId, rot, &w, &h)) return Status::NoSuchDef;
    if (Status s = ValidatePlacement(cit->second, w, h, x, y, id); s != Status::Ok) return s;

    ItemInstance item = iit->second;
    item.Container = dst; item.X = x; item.Y = y; item.Rot = rot;
    PutItem(item);
    return Status::Ok;
}

Status ItemStore::FindFreeSlot(const std::string& defId, ContainerId container, bool allowRotate, ItemId ignoreItem, Placement* out) const
{
    auto cit = Containers.find(container);
    if (cit == Containers.end()) return Status::NoSuchContainer;
    const ItemDef* d = Catalog ? Catalog->Find(defId) : nullptr;
    if (!d) return Status::NoSuchDef;
    const int rotations = (allowRotate && d->W != d->H) ? 2 : 1;
    for (int r = 0; r < rotations; ++r)
    {
        const bool rot = r == 1;
        const int w = rot ? d->H : d->W, h = rot ? d->W : d->H;
        for (int y = 0; y + h <= cit->second.H; ++y)
            for (int x = 0; x + w <= cit->second.W; ++x)
                if (ValidatePlacement(cit->second, w, h, x, y, ignoreItem) == Status::Ok) { *out = {x, y, rot}; return Status::Ok; }
    }
    return Status::NoSpace;
}

Status ItemStore::AutoMove(ItemId id, ContainerId dst, bool allowRotate, ContainerId expectedSrc)
{
    auto iit = Items.find(id);
    if (iit == Items.end()) return Status::NoSuchItem;
    if (expectedSrc != InvalidId && iit->second.Container != expectedSrc) return Status::WrongSource;
    Placement p;
    if (Status s = FindFreeSlot(iit->second.DefId, dst, allowRotate, id, &p); s != Status::Ok) return s;
    return Move(id, dst, p.X, p.Y, p.Rot, expectedSrc);
}

Status ItemStore::AutoPlace(const std::string& defId, int qty, ContainerId container, bool allowRotate, ItemId* outId, float durability)
{
    Placement p;
    if (Status s = FindFreeSlot(defId, container, allowRotate, InvalidId, &p); s != Status::Ok) return s;
    return Create(defId, qty, container, p.X, p.Y, p.Rot, outId, durability);
}

Status ItemStore::Merge(ItemId srcId, ItemId dstId, int qty)
{
    if (srcId == dstId) return Status::InvalidArgument;
    auto s = Items.find(srcId), d = Items.find(dstId);
    if (s == Items.end() || d == Items.end()) return Status::NoSuchItem;
    if (s->second.DefId != d->second.DefId || s->second.Flags != d->second.Flags || s->second.Aux != d->second.Aux) return Status::WrongDef;
    const ItemDef* def = Catalog ? Catalog->Find(s->second.DefId) : nullptr;
    if (!def) return Status::NoSuchDef;
    const int n = qty <= 0 ? s->second.Qty : qty;
    if (n > s->second.Qty) return Status::InvalidArgument;
    if (d->second.Qty + n > def->StackMax) return Status::StackLimit;

    ItemInstance dst = d->second;
    dst.Qty += n;
    if (n == s->second.Qty) EraseItem(srcId);
    else { ItemInstance src = s->second; src.Qty -= n; PutItem(src); }
    PutItem(dst);
    return Status::Ok;
}

Status ItemStore::Split(ItemId srcId, int qty, ContainerId dst, int x, int y, bool rot, ItemId* outId)
{
    auto s = Items.find(srcId);
    if (s == Items.end()) return Status::NoSuchItem;
    if (qty < 1 || qty >= s->second.Qty) return Status::InvalidArgument;
    auto cit = Containers.find(dst);
    if (cit == Containers.end()) return Status::NoSuchContainer;
    int w = 0, h = 0;
    if (!Footprint(s->second.DefId, rot, &w, &h)) return Status::NoSuchDef;
    // The source stays where it is, so it participates in the overlap test.
    if (Status st = ValidatePlacement(cit->second, w, h, x, y, InvalidId); st != Status::Ok) return st;

    ItemInstance src = s->second;
    ItemInstance fresh = src;
    fresh.Id = NextItem++;
    fresh.Qty = qty; fresh.Container = dst; fresh.X = x; fresh.Y = y; fresh.Rot = rot;
    src.Qty -= qty;
    PutItem(src);
    PutItem(fresh);
    if (outId) *outId = fresh.Id;
    return Status::Ok;
}

Status ItemStore::Remove(ItemId id)
{
    if (!Items.count(id)) return Status::NoSuchItem;
    EraseItem(id);
    return Status::Ok;
}

Status ItemStore::SetDurability(ItemId id, float durability)
{
    auto it = Items.find(id);
    if (it == Items.end()) return Status::NoSuchItem;
    ItemInstance item = it->second;
    item.Durability = durability;
    PutItem(item);
    return Status::Ok;
}

Status ItemStore::SetFlags(ItemId id, uint32_t flags)
{
    auto it = Items.find(id);
    if (it == Items.end()) return Status::NoSuchItem;
    ItemInstance item = it->second;
    item.Flags = flags;
    PutItem(item);
    return Status::Ok;
}

Status ItemStore::SetQty(ItemId id, int qty)
{
    auto it = Items.find(id);
    if (it == Items.end()) return Status::NoSuchItem;
    const ItemDef* def = Catalog ? Catalog->Find(it->second.DefId) : nullptr;
    if (!def) return Status::NoSuchDef;
    if (qty < 1) return Status::InvalidArgument;
    if (qty > def->StackMax) return Status::StackLimit;
    ItemInstance item = it->second;
    item.Qty = qty;
    PutItem(item);
    return Status::Ok;
}

Status ItemStore::SetAux(ItemId id, int32_t aux)
{
    auto it = Items.find(id);
    if (it == Items.end()) return Status::NoSuchItem;
    ItemInstance item = it->second;
    item.Aux = aux;
    PutItem(item);
    return Status::Ok;
}

Status ItemStore::ResolveDeath(ContainerId secure, ContainerId returns, const std::vector<ContainerId>& body)
{
    const Container* sc = GetContainer(secure);
    const Container* rc = GetContainer(returns);
    if (!sc || !rc) return Status::NoSuchContainer;
    if (sc->Kind != ContainerKind::Secure || rc->Kind != ContainerKind::Returns) return Status::WrongKind;
    for (ContainerId b : body)
    {
        if (!GetContainer(b)) return Status::NoSuchContainer;
        if (b == secure || b == returns) return Status::InvalidArgument;
    }

    BeginTx();
    const std::vector<ItemId> carried = ItemsIn(secure);   // copy: moves mutate the index
    for (ItemId id : carried)
    {
        if (Status s = AutoMove(id, returns, true); s != Status::Ok) { Rollback(); return s; }
    }
    for (ContainerId b : body)
    {
        if (Status s = SetContainerKind(b, ContainerKind::Corpse); s != Status::Ok) { Rollback(); return s; }
    }
    if (Status s = RemoveContainer(secure); s != Status::Ok) { Rollback(); return s; }
    return Commit();
}

// ------------------------------------------------------------------ transactions
void ItemStore::BeginTx() { Marks.push_back(Log.size()); }

Status ItemStore::Commit()
{
    if (Marks.empty()) return Status::InvalidArgument;
    Marks.pop_back();
    if (Marks.empty()) Log.clear();
    return Status::Ok;
}

Status ItemStore::Rollback()
{
    if (Marks.empty()) return Status::InvalidArgument;
    const size_t mark = Marks.back();
    Marks.pop_back();
    while (Log.size() > mark)
    {
        Undo u = std::move(Log.back());
        Log.pop_back();
        if (u.IsContainer)
        {
            DirtyContainers.insert(u.Id);
            if (u.Cont) RawPutContainer(*u.Cont); else RawEraseContainer(u.Id);
        }
        else
        {
            DirtyItems.insert(u.Id);
            if (u.Item) RawPutItem(*u.Item); else RawEraseItem(u.Id);
        }
    }
    if (Marks.empty()) Log.clear();
    return Status::Ok;
}

ChangeSet ItemStore::DrainChanges()
{
    ChangeSet cs;
    if (InTx()) return cs;
    for (ContainerId id : DirtyContainers)
    {
        ContainerChange c; c.Id = id;
        auto it = Containers.find(id);
        if (it != Containers.end()) c.After = it->second;
        cs.Containers.push_back(std::move(c));
    }
    for (ItemId id : DirtyItems)
    {
        ItemChange c; c.Id = id;
        auto it = Items.find(id);
        if (it != Items.end()) c.After = it->second;
        cs.Items.push_back(std::move(c));
    }
    DirtyItems.clear();
    DirtyContainers.clear();
    return cs;
}

// ------------------------------------------------------------------ import / queries
void ItemStore::Clear()
{
    Items.clear(); Containers.clear(); Index.clear(); Log.clear(); Marks.clear();
    DirtyItems.clear(); DirtyContainers.clear();
    NextItem = 1; NextContainer = 1;
}

Status ItemStore::ImportContainer(const Container& c)
{
    if (c.Id == InvalidId || c.W < 1 || c.H < 1) return Status::InvalidArgument;
    if (Containers.count(c.Id)) return Status::AlreadyExists;
    RawPutContainer(c);
    NextContainer = std::max(NextContainer, c.Id + 1);
    return Status::Ok;
}

Status ItemStore::ImportItem(const ItemInstance& item)
{
    if (item.Id == InvalidId) return Status::InvalidArgument;
    if (Items.count(item.Id)) return Status::AlreadyExists;
    if (!Containers.count(item.Container)) return Status::NoSuchContainer;
    if (!Catalog || !Catalog->Find(item.DefId)) return Status::NoSuchDef;
    RawPutItem(item);
    NextItem = std::max(NextItem, item.Id + 1);
    return Status::Ok;
}

void ItemStore::SetNextIds(ItemId nextItem, ContainerId nextContainer)
{
    NextItem = std::max(NextItem, nextItem);
    NextContainer = std::max(NextContainer, nextContainer);
}

const ItemInstance* ItemStore::GetItem(ItemId id) const { auto it = Items.find(id); return it == Items.end() ? nullptr : &it->second; }
const Container* ItemStore::GetContainer(ContainerId id) const { auto it = Containers.find(id); return it == Containers.end() ? nullptr : &it->second; }
const std::vector<ItemId>& ItemStore::ItemsIn(ContainerId id) const
{
    static const std::vector<ItemId> Empty;
    auto it = Index.find(id);
    return it == Index.end() ? Empty : it->second;
}

float ItemStore::ContainerWeight(ContainerId id) const
{
    float total = 0;
    for (ItemId i : ItemsIn(id))
    {
        const ItemInstance& it = Items.at(i);
        const ItemDef* d = Catalog ? Catalog->Find(it.DefId) : nullptr;
        if (d) total += d->Weight * static_cast<float>(it.Qty);
    }
    return total;
}

std::map<std::string, int64_t> ItemStore::TotalQtyByDef() const
{
    std::map<std::string, int64_t> totals;
    for (const auto& [id, it] : Items) totals[it.DefId] += it.Qty;
    return totals;
}

bool ItemStore::CheckInvariants(std::string* why) const
{
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    size_t indexed = 0;
    for (const auto& [cid, ids] : Index)
    {
        if (!Containers.count(cid)) return fail("index for missing container " + std::to_string(cid));
        if (!std::is_sorted(ids.begin(), ids.end())) return fail("unsorted index in container " + std::to_string(cid));
        for (ItemId id : ids)
        {
            auto it = Items.find(id);
            if (it == Items.end()) return fail("index references missing item " + std::to_string(id));
            if (it->second.Container != cid) return fail("item " + std::to_string(id) + " indexed in wrong container");
        }
        indexed += ids.size();
    }
    if (indexed != Items.size()) return fail("index size " + std::to_string(indexed) + " != item count " + std::to_string(Items.size()));

    for (const auto& [id, it] : Items)
    {
        if (id != it.Id || id == InvalidId) return fail("bad item id " + std::to_string(id));
        auto cit = Containers.find(it.Container);
        if (cit == Containers.end()) return fail("item " + std::to_string(id) + " in missing container");
        const ItemDef* d = Catalog ? Catalog->Find(it.DefId) : nullptr;
        if (!d) return fail("item " + std::to_string(id) + " has unknown def " + it.DefId);
        if (it.Qty < 1 || it.Qty > d->StackMax) return fail("item " + std::to_string(id) + " qty out of range");
        int w = 0, h = 0;
        Footprint(it.DefId, it.Rot, &w, &h);
        if (it.X < 0 || it.Y < 0 || it.X + w > cit->second.W || it.Y + h > cit->second.H) return fail("item " + std::to_string(id) + " out of bounds");
    }
    for (const auto& [cid, ids] : Index)
    {
        for (size_t a = 0; a < ids.size(); ++a)
        {
            const ItemInstance& ia = Items.at(ids[a]);
            int aw = 0, ah = 0;
            Footprint(ia.DefId, ia.Rot, &aw, &ah);
            for (size_t b = a + 1; b < ids.size(); ++b)
            {
                const ItemInstance& ib = Items.at(ids[b]);
                int bw = 0, bh = 0;
                Footprint(ib.DefId, ib.Rot, &bw, &bh);
                if (ia.X < ib.X + bw && ib.X < ia.X + aw && ia.Y < ib.Y + bh && ib.Y < ia.Y + ah)
                    return fail("items " + std::to_string(ia.Id) + " and " + std::to_string(ib.Id) + " overlap in container " + std::to_string(cid));
            }
        }
    }
    return true;
}
} // namespace Scavar
