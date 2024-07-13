#ifndef MODULE_RECYCLED_ITEMS_H
#define MODULE_RECYCLED_ITEMS_H

#include "ScriptMgr.h"

struct RecycleItemInfo {
    uint32 entry;
    uint32 count;
    uint64 owner;
};
std::vector<RecycleItemInfo> itemsToRecycle;

std::string GetCurrencyStringFromCopper(uint32 /*copper*/);
double GetExpMultiplier(uint32 /*itemLevel*/);
uint32 GetRecycleSaleCost(Item* /*item*/);
bool IsItemRecylable(Item* /*item*/);
void RecycleItem(Item* /*item*/, Player* /*player*/);
void RecycleItems();
void RefreshRecycledItems();

class RecycledItemsCreatureScript : public CreatureScript {
public:
    RecycledItemsCreatureScript() : CreatureScript("RecycledItemsCreatureScript") { }

private:
    bool OnGossipHello(Player* /*player*/, Creature* /*creature*/) override;
    bool OnGossipSelect(Player* /*player*/, Creature* /*creature*/, uint32 /*sender*/, uint32 /*action*/) override;

private:
    enum RecyclerGossips
    {
        GOSSIP_RECYCLER_TEXT_HELLO = 888111,
        GOSSIP_RECYCLER_TEXT_HELP = 888112,

        GOSSIP_RECYCLER_ACTION_RECYCLE = 1,
        GOSSIP_RECYCLER_ACTION_RECYCLE_HELP = 2,
        GOSSIP_RECYCLER_ACTION_AUCTION = 3
    };
};

class RecycledItemsPlayerScript : public PlayerScript
{
public:
    RecycledItemsPlayerScript() : PlayerScript("RecycledItemsPlayerScript") { }

private:
    bool CanSellItem(Player* /*player*/, Item* /*item*/, Creature* /*creature*/) override;
    bool CanCastItemUseSpell(Player* /*player*/, Item* /*item*/, SpellCastTargets const& /*targets*/, uint8 /*cast_count*/, uint32 /*glyphIndex*/) override;

    Language GetLanguageForTarget(Player* /*player*/);
};

class RecycledItemsWorldScript : public WorldScript
{
public:
    RecycledItemsWorldScript() : WorldScript("RecycledItemsWorldScript"), counter(0), refreshCounter(0) { }

private:
    void OnUpdate(uint32 /*diff*/) override;

private:
    uint32 counter;
    uint32 refreshCounter;
};

#endif // MODULE_RECYCLED_ITEMS_H
