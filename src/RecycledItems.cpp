#include "RecycledItems.h"

#include <Chat.h>
#include <Config.h>
#include <GameTime.h>
#include <Player.h>
#include <ScriptedGossip.h>
#include <Spell.h>

std::string GetCurrencyStringFromCopper(uint32 copper)
{
    std::stringstream ss;

    uint32 gold = copper / 10000;
    copper = copper - (gold * 10000);

    uint32 silver = copper / 100;
    copper = copper - (silver * 100);

    if (gold > 0)
    {
        ss << Acore::StringFormatFmt("|cffEED044{} gold", gold);
    }

    if ((gold > 0 && silver > 0) ||
        (gold > 0 && copper > 0))
    {
        ss << ", |r";
    }

    if (silver > 0)
    {
        ss << Acore::StringFormatFmt("|cffC2BFC2{} silver", silver);
    }

    if ((silver > 0 && copper > 0))
    {
        ss << ", |r";
    }

    if (copper > 0)
    {
        ss << Acore::StringFormatFmt("|cffC5865F{} copper", copper);
    }

    return ss.str();
}

double GetExpMultiplier(uint32 itemLevel)
{
    return 1 + (pow(static_cast<double>(itemLevel) / 300.0, 10) * 100.0);
}

uint32 GetRecycleSaleCost(Item* item)
{
    auto itemProto = item->GetTemplate();

    return ((itemProto->SellPrice * sConfigMgr->GetOption<uint32>("RecycledItems.Auction.BuyoutMultiplier", 10)) // Initial Buyout
        * GetExpMultiplier(itemProto->ItemLevel)) * item->GetCount(); // Exponential Cost
}

void RecycleItems()
{
    if (itemsToRecycle.size() == 0)
    {
        return;
    }

    auto auctionHouse = sAuctionMgr->GetAuctionsMapByHouseId(AUCTIONHOUSE_NEUTRAL);
    auto auctionHouseEntry = sAuctionMgr->GetAuctionHouseEntryFromHouse(AUCTIONHOUSE_NEUTRAL);

    uint32 bidMultiplier = sConfigMgr->GetOption<uint32>("RecycledItems.Auction.BidMultiplier", 5);
    uint32 buyoutMultiplier = sConfigMgr->GetOption<uint32>("RecycledItems.Auction.BuyoutMultiplier", 10);
    ObjectGuid sellerGuid = ObjectGuid(sConfigMgr->GetOption<uint64>("RecycledItems.Auction.SellerGuid", 0));

    uint32 count = 0;
    for (auto itemInfo : itemsToRecycle)
    {
        auto item = Item::CreateItem(itemInfo.entry, itemInfo.count);

        AuctionEntry* auctionItem = new AuctionEntry;
        auctionItem->Id = sObjectMgr->GenerateAuctionID();
        auctionItem->houseId = AUCTIONHOUSE_NEUTRAL;
        auctionItem->item_guid = item->GetGUID();
        auctionItem->item_template = item->GetEntry();
        auctionItem->itemCount = item->GetCount();

        if (sConfigMgr->GetOption<bool>("RecycledItems.Auction.UseOriginalSeller", false))
        {
            sellerGuid = ObjectGuid(itemInfo.owner);
        }
        auctionItem->owner = sellerGuid;

        uint32 saleCost = GetRecycleSaleCost(item);

        auctionItem->startbid = saleCost / 2;
        auctionItem->bidder = ObjectGuid::Empty;
        auctionItem->bid = 0;
        auctionItem->buyout = saleCost;
        auctionItem->expire_time = GameTime::GetGameTime().count() + (HOUR * 48);
        auctionItem->deposit = 0;
        auctionItem->auctionHouseEntry = auctionHouseEntry;

        sAuctionMgr->AddAItem(item);
        auctionHouse->AddAuction(auctionItem);

        count++;
    }

    itemsToRecycle.clear();
}

void RefreshRecycledItems()
{
    if (sConfigMgr->GetOption<bool>("RecycledItems.Auction.Refresh", true) &&
        !sConfigMgr->GetOption<bool>("RecycledItems.Auction.UseOriginalSeller", false))
    {
        auto auctionHouse = sAuctionMgr->GetAuctionsMapByHouseId(AUCTIONHOUSE_NEUTRAL);
        ObjectGuid sellerGuid = ObjectGuid(sConfigMgr->GetOption<uint64>("RecycledItems.Auction.SellerGuid", 0));

        auto& auctions = auctionHouse->GetAuctions();

        for (const auto& auction : auctions)
        {
            auto auctionInfo = auction.second;

            // Do not extend auction with active bids (otherwise they wont ever receive the item!!)
            if (auctionInfo->bidder)
            {
                continue;
            }

            // Only extend server auctions.
            if (auctionInfo->owner != sellerGuid)
            {
                continue;
            }

            auctionInfo->expire_time = GameTime::GetGameTime().count() + (HOUR * 48);
        }
    }
}

void RecycleItem(Item* item, Player* player)
{
    RecycleItemInfo itemInfo;

    itemInfo.entry = item->GetEntry();
    itemInfo.count = item->GetCount();
    itemInfo.owner = player->GetGUID().GetRawValue();

    itemsToRecycle.push_back(itemInfo);
}

bool IsItemRecylable(Item* item)
{
    if (!item)
    {
        return false;
    }

    auto itemProto = item->GetTemplate();

    if (itemProto->SellPrice < 1)
    {
        return false;
    }

    uint32 minItemLevel = sConfigMgr->GetOption<uint32>("RecycledItems.Filter.MinimumItemLevel", 0);
    uint32 minQuality = sConfigMgr->GetOption<uint32>("RecycledItems.Filter.MinimumQuality", ITEM_QUALITY_NORMAL);

    if (itemProto->ItemLevel < minItemLevel || itemProto->Quality < minQuality)
    {
        return false;
    }

    if (sConfigMgr->GetOption<uint32>("RecycledItems.Filter.OnlyTradable", true))
    {
        if (itemProto->Bonding != BIND_WHEN_EQUIPED &&
            itemProto->Bonding != NO_BIND &&
            itemProto->Bonding != BIND_WHEN_USE)
        {
            return false;
        }
    }

    return true;
}

Language RecycledItemsPlayerScript::GetLanguageForTarget(Player* player)
{
    switch (player->GetTeamId())
    {
    case Team::ALLIANCE:
        return Language::LANG_COMMON;

    case Team::HORDE:
        return Language::LANG_ORCISH;
    }

    return Language::LANG_UNIVERSAL;
}

bool RecycledItemsPlayerScript::CanSellItem(Player* player, Item* item, Creature* creature)
{
    if (!sConfigMgr->GetOption<bool>("RecycledItems.Enable", false))
    {
        return true;
    }

    if (!item || !creature || !player)
    {
        return true;
    }

    if (creature->GetEntry() != sConfigMgr->GetOption<uint32>("RecycledItems.Entry", 888999))
    {
        return true;
    }

    auto itemProto = item->GetTemplate();

    if (!IsItemRecylable(item))
    {
        player->SendSellError(SELL_ERR_CANT_SELL_ITEM, creature, item->GetGUID(), 0);
        creature->Whisper("You cannot recycle that item.", GetLanguageForTarget(player), player);
        return false;
    }

    RecycleItem(item, player);

    player->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);

    uint32 bonusMultiplier = sConfigMgr->GetOption<uint32>("RecycledItems.Vendor.CashMultiplier", 2);
    uint32 sellPrice = itemProto->SellPrice * item->GetCount();
    uint32 money = sellPrice * bonusMultiplier;
    player->ModifyMoney(money);

    std::string msg = Acore::StringFormatFmt("|cffFFFFFFGained {} |cffFFFFFFfor recycling.|r", GetCurrencyStringFromCopper(money));
    WorldPacket notifyPacket(SMSG_NOTIFICATION, msg.size() + 1);
    notifyPacket << msg;
    player->SendDirectMessage(&notifyPacket);

    return false;
}

bool RecycledItemsPlayerScript::CanCastItemUseSpell(Player* player, Item* item, SpellCastTargets const& targets, uint8 /*cast_count*/, uint32 /*glyphIndex*/)
{
    if (!item)
    {
        return true;
    }

    auto itemProto = item->GetTemplate();
    if (!itemProto)
    {
        return true;
    }

    if (itemProto->ItemId != 41178)
    {
        return true;
    }

    if (!sConfigMgr->GetOption<bool>("RecycledItems.Enable", false))
    {
        player->SendSystemMessage("This item is disabled.");
        return true;
    }

    auto targetItem = targets.GetItemTarget();
    if (!targetItem)
    {
        return false;
    }

    if (targetItem->GetOwner()->GetGUID() != player->GetGUID())
    {
        player->SendSystemMessage("You do not own that item.");
        return false;
    }

    if (!IsItemRecylable(targetItem))
    {
        player->SendSystemMessage("You cannot recycle that item.");
        return false;
    }

    RecycleItem(targetItem, player);

    player->DestroyItemCount(itemProto->ItemId, 1, true); // Remote Recycler Item
    player->RemoveItem(targetItem->GetBagSlot(), targetItem->GetSlot(), true);

    auto targetItemProto = targetItem->GetTemplate();

    uint32 bonusMultiplier = sConfigMgr->GetOption<uint32>("RecycledItems.Vendor.CashMultiplier", 2);
    uint32 sellPrice = targetItemProto->SellPrice * targetItem->GetCount();
    uint32 money = sellPrice * bonusMultiplier;
    player->ModifyMoney(money);

    std::string msg = Acore::StringFormatFmt("|cffFFFFFFGained {} |cffFFFFFFfor recycling.|r", GetCurrencyStringFromCopper(money));
    WorldPacket notifyPacket(SMSG_NOTIFICATION, msg.size() + 1);
    notifyPacket << msg;
    player->SendDirectMessage(&notifyPacket);

    return false;
}

bool RecycledItemsCreatureScript::OnGossipHello(Player* player, Creature* creature)
{
    if (!sConfigMgr->GetOption<bool>("RecycledItems.Enable", false))
    {
        return false;
    }

    ClearGossipMenuFor(player);

    AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "I would like to recycle some items.", GOSSIP_SENDER_MAIN, GOSSIP_RECYCLER_ACTION_RECYCLE);

    if (sConfigMgr->GetOption<bool>("RecycledItems.Access.AuctionHouse", true))
    {
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "I would like to view the auction house.", GOSSIP_SENDER_MAIN, GOSSIP_RECYCLER_ACTION_AUCTION);
    }

    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "How does this work?", GOSSIP_SENDER_MAIN, GOSSIP_RECYCLER_ACTION_RECYCLE_HELP);

    SendGossipMenuFor(player, GOSSIP_RECYCLER_TEXT_HELLO, creature);

    return true;
}
bool RecycledItemsCreatureScript::OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action)
{
    if (action == 0)
    {
        return true;
    }

    ClearGossipMenuFor(player);

    switch (action)
    {
    case GOSSIP_RECYCLER_ACTION_RECYCLE:
        player->GetSession()->SendListInventory(creature->GetGUID());
        break;

    case GOSSIP_RECYCLER_ACTION_RECYCLE_HELP:
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormatFmt("Items are worth 200% when vendored here.", sConfigMgr->GetOption<uint32>("RecycledItems.Vendor.CashMultiplier", 10) * 100), GOSSIP_SENDER_MAIN, 0);

        SendGossipMenuFor(player, GOSSIP_RECYCLER_TEXT_HELP, creature);
        break;

    case GOSSIP_RECYCLER_ACTION_AUCTION:
        player->GetSession()->SendAuctionHello(creature->GetGUID(), creature);
        break;
    }

    return true;
}

void RecycledItemsWorldScript::OnUpdate(uint32 diff)
{
    if (!sConfigMgr->GetOption<bool>("RecycledItems.Enable", false))
    {
        return;
    }

    counter += diff;
    refreshCounter += diff;

    uint32 updateFrequency = sConfigMgr->GetOption<uint32>("RecycledItems.UpdateFrequency", 30) * 1000;

    if (counter >= updateFrequency)
    {
        counter = 0;

        RecycleItems();
    }

    uint32 refreshFrequency = sConfigMgr->GetOption<uint32>("RecycledItems.RefreshFrequency", 3600) * 1000;

    if (refreshCounter >= refreshFrequency)
    {
        refreshCounter = 0;

        RefreshRecycledItems();
    }
}

bool RecycledItemsItemScript::CanItemRemove(Player* player, Item* item)
{
    if (!sConfigMgr->GetOption<bool>("RecycledItems.Enable", false))
    {
        return true;
    }

    if (!sConfigMgr->GetOption<bool>("RecycledItems.Filter.Deleted", true))
    {
        return true;
    }

    if (!player || !item)
    {
        return true;
    }

    if (!IsItemRecylable(item))
    {
        return true;
    }

    auto itemProto = item->GetTemplate();

    RecycleItem(item, player);

    player->SendSystemMessage(Acore::StringFormatFmt("|c{0:x}{1} |cffFF0000was deleted and sent to the |cffFFFFFF|Hitem:999888:0:0:0:0:0:0:0:0|h[Recycler]|h|r", ItemQualityColors[itemProto->Quality], itemProto->Name1));

    return true;
}

void SC_AddRecycledItemsScripts()
{
    new RecycledItemsWorldScript();
    new RecycledItemsCreatureScript();
    new RecycledItemsPlayerScript();
    new RecycledItemsItemScript();
}
