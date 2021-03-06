//
// Created by PolarAir on 2019-04-18.
//

#ifndef KUNGFU_PORTFOLIO_MANAGER_HPP
#define KUNGFU_PORTFOLIO_MANAGER_HPP

#include "../include/portfolio_manager.h"
#include "../include/account_manager.h"
#include "portfolio_storage.h"
#include "util/include/business_helper.h"
#include "serialize.h"
#include <spdlog/spdlog.h>
#include <map>
#include <boost/core/ignore_unused.hpp>

namespace kungfu
{
    class PortfolioManager::impl final : public IPnLDataHandler
    {
    public:
        explicit impl(const char* db);
        virtual ~impl();

        Position get_long_pos(const char* account_id, const char* instrument_id, const char* exchange_id) const;
        Position get_short_pos(const char* account_id, const char* instrument_id, const char* exchange_id) const;
        double get_last_price(const char* instrument_id, const char* exchange_id) const;
        std::vector<Instrument> get_all_pos_instruments(const char* account_id) const;

        SubPortfolioInfo get_sub_portfolio(const char* account_id) const;
        PortfolioInfo get_portfolio() const;

        const AccountManagerPtr get_account(const char* account_id) const;

        // IPnLDataHandler
        void on_quote(const Quote* quote) override;
        void on_order(const Order* order) override;
        void on_trade(const Trade* trade) override;
        void on_positions(const std::vector<Position>& positions) override;
        void on_position_details(const std::vector<Position>& details) override;
        void on_account(const AccountInfo& account) override;
        void insert_order(const OrderInput* input) override;
        bool freeze_algo_order(uint64_t algo_id, const AssetsFrozen& frozen) override;
        void release_algo_order(uint64_t algo_id) override;
        void switch_day(const std::string& trading_day) override;
        int64_t get_last_update() const override;
        std::string get_current_trading_day() const override;
        void set_current_trading_day(const std::string& trading_day) override;
        void register_pos_callback(PositionCallback cb) override;
        void register_acc_callback(AccountCallback cb) override;
        void register_pnl_callback(PnLCallback cb) override;
        void set_initial_equity(double equity) override;
        void set_static_equity(double equity) override;
        // IPnLDataHandler

    private:
        bool recalc_pnl();
        void callback() const;
        void on_acc_callback(const AccountInfo& acc) const;
        void on_pos_callback(const Position& pos) const;

    private:
        std::string                                     db_;
        int64_t                                         last_update_;
        std::string                                     trading_day_;
        std::map<std::string, AccountManagerPtr>        accounts_;
        PortfolioStorage                                storage_;
        PortfolioInfo                                   pnl_;
        std::vector<PnLCallback>                        cbs_;
        std::vector<AccountCallback>                    acc_cbs_;
        std::vector<PositionCallback>                   pos_cbs_;
    };

    // impl
    PortfolioManager::impl::impl(const char *db) :
            db_(db), last_update_(0), storage_(db), pnl_{}
    {
        storage_.load(last_update_, trading_day_, pnl_, accounts_);
        for (auto& iter : accounts_)
        {
            iter.second->register_pos_callback(std::bind(&PortfolioManager::impl::on_pos_callback, this, std::placeholders::_1));
            iter.second->register_acc_callback(std::bind(&PortfolioManager::impl::on_acc_callback, this, std::placeholders::_1));
        }
    }

    PortfolioManager::impl::~impl()
    {
        storage_.save(last_update_, trading_day_, pnl_);
    }

    Position PortfolioManager::impl::get_long_pos(const char *account_id, const char *instrument_id, const char *exchange_id) const
    {
        Position pos = {};
        strcpy(pos.instrument_id, instrument_id);
        strcpy(pos.exchange_id, exchange_id);
        pos.instrument_type = get_instrument_type(instrument_id, exchange_id);
        pos.direction = DirectionLong;
        if (nullptr != account_id)
        {
            strcpy(pos.account_id, account_id);
        }
        for (const auto& iter : accounts_)
        {
            if (nullptr != iter.second)
            {
                if (nullptr == account_id || strlen(account_id) == 0 || strcmp(account_id, iter.first.c_str()) == 0)
                {
                    auto cur_pos = iter.second->get_long_pos(instrument_id, exchange_id);
                    pos.update_time = std::max<int64_t>(pos.update_time, cur_pos.update_time);
                    strcpy(pos.client_id, cur_pos.client_id);
                    pos.volume += cur_pos.volume;
                    pos.yesterday_volume += cur_pos.yesterday_volume;
                    pos.frozen_total += cur_pos.frozen_total;
                    pos.frozen_yesterday += cur_pos.frozen_yesterday;
                    pos.last_price = cur_pos.last_price;
                    pos.open_price = (pos.open_price * pos.volume + cur_pos.open_price * cur_pos.volume) / (pos.volume + cur_pos.volume);
                    pos.cost_price = (pos.cost_price * pos.volume + cur_pos.cost_price * cur_pos.volume) / (pos.volume + cur_pos.volume);
                    pos.close_price = cur_pos.close_price;
                    pos.pre_close_price = cur_pos.pre_close_price;
                    pos.settlement_price = cur_pos.settlement_price;
                    pos.pre_settlement_price = cur_pos.pre_settlement_price;
                    pos.margin += cur_pos.margin;
                    pos.position_pnl += cur_pos.position_pnl;
                    pos.close_pnl += cur_pos.close_pnl;
                    pos.realized_pnl += cur_pos.realized_pnl;
                    pos.unrealized_pnl += cur_pos.unrealized_pnl;
                }
            }
        }
        return pos;
    }

    Position PortfolioManager::impl::get_short_pos(const char *account_id, const char *instrument_id, const char *exchange_id) const
    {
        Position pos = {};
        strcpy(pos.instrument_id, instrument_id);
        strcpy(pos.exchange_id, exchange_id);
        pos.direction = DirectionShort;
        for (const auto& iter : accounts_)
        {
            if (nullptr != iter.second)
            {
                if (nullptr == account_id || strlen(account_id) == 0 || strcmp(account_id, iter.first.c_str()) == 0)
                {
                    auto cur_pos = iter.second->get_short_pos(instrument_id, exchange_id);
                    strcpy(pos.client_id, cur_pos.client_id);
                    pos.volume += cur_pos.volume;
                    pos.yesterday_volume += cur_pos.yesterday_volume;
                    pos.frozen_total += cur_pos.frozen_total;
                    pos.frozen_yesterday += cur_pos.frozen_yesterday;
                    pos.last_price = cur_pos.last_price;
                    pos.open_price = (pos.open_price * pos.volume + cur_pos.open_price * cur_pos.volume) / (pos.volume + cur_pos.volume);
                    pos.cost_price = (pos.cost_price * pos.volume + cur_pos.cost_price * cur_pos.volume) / (pos.volume + cur_pos.volume);
                    pos.close_price = cur_pos.close_price;
                    pos.pre_close_price = cur_pos.pre_close_price;
                    pos.settlement_price = cur_pos.settlement_price;
                    pos.pre_settlement_price = cur_pos.pre_settlement_price;
                    pos.margin += cur_pos.margin;
                    pos.position_pnl += cur_pos.position_pnl;
                    pos.close_pnl += cur_pos.close_pnl;
                    pos.realized_pnl += cur_pos.realized_pnl;
                    pos.unrealized_pnl += cur_pos.unrealized_pnl;
                }
            }
        }
        return pos;
    }

    double PortfolioManager::impl::get_last_price(const char *instrument_id, const char *exchange_id) const
    {
        for (const auto& iter : accounts_)
        {
            if (nullptr != iter.second)
            {
                double price = iter.second->get_last_price(instrument_id, exchange_id);
                if (!is_zero(price))
                {
                    return price;
                }
            }
        }

        return 0;
    }

    std::vector<Instrument> PortfolioManager::impl::get_all_pos_instruments(const char *account_id) const
    {
        std::map<std::string, Instrument> m;
        for (const auto& iter : accounts_)
        {
            if (nullptr != iter.second)
            {
                if (nullptr == account_id || strlen(account_id) == 0 || strcmp(account_id, iter.first.c_str()) == 0)
                {
                    auto ins_vec = iter.second->get_all_pos_instruments();
                    for (const auto& pos : ins_vec)
                    {
                        auto key = get_symbol(pos.instrument_id, pos.exchange_id);
                        if (m.find(key) == m.end())
                        {
                            m[key] = pos;
                        }
                    }
                }
            }
        }
        std::vector<Instrument> ret;
        for (const auto& iter : m)
        {
            ret.emplace_back(iter.second);
        }
        return ret;
    }

    SubPortfolioInfo PortfolioManager::impl::get_sub_portfolio(const char *account_id) const
    {
        if (accounts_.find(account_id) != accounts_.end())
        {
            return accounts_.at(account_id)->get_account_info();
        }
        else
        {
            SubPortfolioInfo subPortfolioInfo = {};
            strcpy(subPortfolioInfo.account_id, account_id);
            return subPortfolioInfo;
        }
    }

    PortfolioInfo PortfolioManager::impl::get_portfolio() const
    {
        return pnl_;
    }

    const AccountManagerPtr PortfolioManager::impl::get_account(const char *account_id) const
    {
        if (accounts_.find(account_id) != accounts_.end())
        {
            return accounts_.at(account_id);
        }
        return AccountManagerPtr();
    }

#define IMPLEMENT_DATA_BODY(func_name, ...) \
    for (const auto& iter : accounts_) \
    { \
        if (nullptr != iter.second) \
        { \
            iter.second->func_name(__VA_ARGS__); \
        } \
    } \
    if (recalc_pnl()) \
    { \
        callback(); \
    }

    void PortfolioManager::impl::on_quote(const kungfu::Quote *quote)
    {
        last_update_ = quote->rcv_time;
        IMPLEMENT_DATA_BODY(on_quote, quote)
    }

    void PortfolioManager::impl::on_order(const kungfu::Order *order)
    {
        last_update_ = order->rcv_time;
        IMPLEMENT_DATA_BODY(on_order, order)
    }

    void PortfolioManager::impl::on_trade(const kungfu::Trade *trade)
    {
        SPDLOG_TRACE("trade: {}", to_string(*trade));
        SPDLOG_TRACE("pnl before: {}", to_string(pnl_));
        last_update_ = trade->rcv_time;
        IMPLEMENT_DATA_BODY(on_trade, trade)
        // 成交时一定写一下数据库, 解决前端切换页面时读库问题
        storage_.save(last_update_, trading_day_, pnl_);
        SPDLOG_TRACE("pnl after: {}", to_string(pnl_));
    }

    void PortfolioManager::impl::on_positions(const std::vector<kungfu::Position> &positions)
    {
        IMPLEMENT_DATA_BODY(on_positions, positions)
    }

    void PortfolioManager::impl::on_position_details(const std::vector<kungfu::Position> &details)
    {
        IMPLEMENT_DATA_BODY(on_position_details, details)
    }

    void PortfolioManager::impl::on_account(const kungfu::AccountInfo &account)
    {
        if (accounts_.find(account.account_id) == accounts_.end())
        {
            auto account_manager = std::make_shared<AccountManager>(account.account_id, account.type, db_.c_str());
            accounts_[account.account_id] = account_manager;
            account_manager->register_pos_callback(std::bind(&PortfolioManager::impl::on_pos_callback, this, std::placeholders::_1));
            account_manager->register_acc_callback(std::bind(&PortfolioManager::impl::on_acc_callback, this, std::placeholders::_1));
        }
        accounts_[account.account_id]->on_account(account);
        SPDLOG_TRACE("pnl after on_account: {}", to_string(pnl_));
    }

    void PortfolioManager::impl::insert_order(const kungfu::OrderInput *input)
    {
        IMPLEMENT_DATA_BODY(insert_order, input)
    }

    bool PortfolioManager::impl::freeze_algo_order(uint64_t algo_id, const kungfu::AssetsFrozen &frozen)
    {
        IMPLEMENT_DATA_BODY(freeze_algo_order, algo_id, frozen)
        return true;
    }

    void PortfolioManager::impl::release_algo_order(uint64_t algo_id)
    {
        IMPLEMENT_DATA_BODY(release_algo_order, algo_id)
    }

    void PortfolioManager::impl::switch_day(const std::string & trading_day)
    {
        if (trading_day <= trading_day_)
        {
            return;
        }
        trading_day_ = trading_day;
        strcpy(pnl_.trading_day, trading_day.c_str());

        for (const auto& iter : accounts_)
        {
            if (nullptr != iter.second)
            {
                iter.second->switch_day(trading_day);
            }
        }

        recalc_pnl();
        callback();

        storage_.save(last_update_, trading_day_, pnl_);
    }

    int64_t PortfolioManager::impl::get_last_update() const
    {
        return last_update_;
    }

    std::string PortfolioManager::impl::get_current_trading_day() const
    {
        return trading_day_;
    }

    void PortfolioManager::impl::set_current_trading_day(const std::string &trading_day)
    {
        for (const auto& iter : accounts_)
        {
            if (nullptr != iter.second)
            {
                iter.second->set_current_trading_day(trading_day);
            }
        }

        if (trading_day >= trading_day_)
        {
            if (!trading_day_.empty())
            {
                switch_day(trading_day);
            }
            else
            {
                trading_day_ = trading_day;
                strcpy(pnl_.trading_day, trading_day.c_str());
            }
        }
    }

    void PortfolioManager::impl::register_pos_callback(kungfu::PositionCallback cb)
    {
        pos_cbs_.emplace_back(std::move(cb));
    }

    void PortfolioManager::impl::register_acc_callback(kungfu::AccountCallback cb)
    {
        acc_cbs_.emplace_back(std::move(cb));
    }

    void PortfolioManager::impl::register_pnl_callback(kungfu::PnLCallback cb)
    {
        cbs_.emplace_back(std::move(cb));
    }

    void PortfolioManager::impl::set_initial_equity(double equity)
    {
        boost::ignore_unused(equity);
    }

    void PortfolioManager::impl::set_static_equity(double equity)
    {
        boost::ignore_unused(equity);
    }

    bool PortfolioManager::impl::recalc_pnl()
    {
        double old_dynamic = pnl_.dynamic_equity;

        pnl_.update_time = yijinjing::getNanoTime();
        strcpy(pnl_.trading_day, trading_day_.c_str());
        pnl_.initial_equity = 0;
        pnl_.static_equity = 0;
        pnl_.dynamic_equity = 0;
        for (const auto& iter : accounts_)
        {
            auto account_info = iter.second->get_account_info();
            pnl_.initial_equity += account_info.initial_equity;
            pnl_.static_equity += account_info.static_equity;
            pnl_.dynamic_equity += account_info.dynamic_equity;
        }
        pnl_.accumulated_pnl = pnl_.dynamic_equity - pnl_.initial_equity;
        pnl_.accumulated_pnl_ratio = is_zero(pnl_.initial_equity) ? 0 : pnl_.accumulated_pnl / pnl_.initial_equity;
        pnl_.intraday_pnl = pnl_.dynamic_equity - pnl_.static_equity;
        pnl_.intraday_pnl_ratio = is_zero(pnl_.static_equity) ? 0 : pnl_.intraday_pnl / pnl_.static_equity;
        return !is_equal(old_dynamic, pnl_.dynamic_equity);
    }

    void PortfolioManager::impl::callback() const
    {
        for (const auto& cb : cbs_)
        {
            cb(pnl_);
        }
    }

    void PortfolioManager::impl::on_pos_callback(const kungfu::Position &pos) const
    {
        auto total_pos = pos.direction == DirectionLong ?
                get_long_pos(nullptr, pos.instrument_id, pos.exchange_id) :
                get_short_pos(nullptr, pos.instrument_id, pos.exchange_id);

        for (auto& cb : pos_cbs_)
        {
            cb(total_pos);
        }
    }

    void PortfolioManager::impl::on_acc_callback(const kungfu::AccountInfo &acc) const
    {
        for (auto& cb : acc_cbs_)
        {
            cb(acc);
        }
    }
}

#endif //KUNGFU_PORTFOLIO_MANAGER_HPP
