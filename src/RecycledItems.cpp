#include "RecycledItems.h"

#include <Chat.h>
#include <Config.h>
#include <GameTime.h>
#include <Player.h>
#include <ScriptedGossip.h>

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
        auto itemProto = item->GetTemplate();

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

        auctionItem->startbid = itemProto->SellPrice * bidMultiplier;
        auctionItem->bidder = ObjectGuid::Empty;
        auctionItem->bid = 0;
        auctionItem->buyout = itemProto->SellPrice * buyoutMultiplier;
        auctionItem->expire_time = GameTime::GetGameTime().count() + (HOUR * 48);
        auctionItem->deposit = 0;
        auctionItem->auctionHouseEntry = auctionHouseEntry;

        sAuctionMgr->AddAItem(item);
        auctionHouse->AddAuction(auctionItem);

        count++;
    }

    itemsToRecycle.clear();

    if (sConfigMgr->GetOption<bool>("RecycledItems.Auction.Refresh", true) &&
        !sConfigMgr->GetOption<bool>("RecycledItems.Auction.UseOriginalSeller", false))
    {
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

Language RecycledItemsPlayerScript::GetLanguageForTarget(Player* player)
{
    return Language::LANG_COMMON;
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

    uint32 minItemLevel = sConfigMgr->GetOption<uint32>("RecycledItems.Filter.MinimumItemLevel", 0);
    uint32 minQuality = sConfigMgr->GetOption<uint32>("RecycledItems.Filter.MinimumQuality", ITEM_QUALITY_NORMAL);

    if (itemProto->SellPrice < 1)
    {
        player->SendSellError(SELL_ERR_CANT_SELL_ITEM, creature, item->GetGUID(), 0);

        return false;
    }

    if (itemProto->ItemLevel < minItemLevel || itemProto->Quality < minQuality)
    {
        player->SendSellError(SELL_ERR_CANT_SELL_ITEM, creature, item->GetGUID(), 0);
        creature->Whisper("You cannot recycle that item.", GetLanguageForTarget(player), player);

        return false;
    }

    if (sConfigMgr->GetOption<uint32>("RecycledItems.Filter.OnlyTradable", true))
    {
        if (itemProto->Bonding != BIND_WHEN_EQUIPED && itemProto->Bonding != NO_BIND)
        {
            player->SendSellError(SELL_ERR_CANT_SELL_ITEM, creature, item->GetGUID(), 0);
            creature->Whisper("You can only recycle tradable items.", GetLanguageForTarget(player), player);

            return false;
        }
    }

    RecycleItemInfo itemInfo;

    itemInfo.entry = item->GetEntry();
    itemInfo.count = item->GetCount();
    itemInfo.owner = player->GetGUID().GetRawValue();

    itemsToRecycle.push_back(itemInfo);

    player->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);

    uint32 bonusMultiplier = sConfigMgr->GetOption<uint32>("RecycledItems.Vendor.CashMultiplier", 10);
    player->ModifyMoney(itemProto->SellPrice + (itemProto->SellPrice / bonusMultiplier));

    return false;
}

bool RecycledItemsCreatureScript::OnGossipHello(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);

    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "I would like to recycle some items.", GOSSIP_SENDER_MAIN, GOSSIP_RECYCLER_ACTION_RECYCLE);
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
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormatFmt("You will gain {}% more money per sale.", sConfigMgr->GetOption<uint32>("RecycledItems.Vendor.CashMultiplier", 10)), GOSSIP_SENDER_MAIN, 0);

        SendGossipMenuFor(player, GOSSIP_RECYCLER_TEXT_HELP, creature);
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

    uint32 updateFrequency = sConfigMgr->GetOption<uint32>("RecycledItems.UpdateFrequency", 30000);

    if (counter >= updateFrequency)
    {
        counter = 0;

        RecycleItems();
    }
}

void SC_AddRecycledItemsScripts()
{
    new RecycledItemsWorldScript();
    new RecycledItemsCreatureScript();
    new RecycledItemsPlayerScript();
}
