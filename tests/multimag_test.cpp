#include <algorithm>
#include <cstddef>
#include <functional>
#include <list>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "debug.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "inventory_ui.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "options_helpers.h"
#include "output.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "ret_val.h"
#include "type_id.h"
#include "visitable.h"

static const itype_id itype_38_special( "38_special" );
static const itype_id itype_556( "556" );
static const itype_id itype_9mm( "9mm" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_flashlight( "flashlight" );
static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_medium_battery_cell( "medium_battery_cell" );
static const itype_id itype_stanag30( "stanag30" );
static const itype_id itype_sw_619( "sw_619" );
static const itype_id itype_test_multimag_gun( "test_multimag_gun" );
static const itype_id itype_test_multimag_gun_same_type( "test_multimag_gun_same_type" );

static item make_loaded_glock()
{
    item mag( itype_glockmag );
    mag.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    item gun( itype_glock_19 );
    gun.put_in( mag, pocket_type::MAGAZINE_WELL );
    return gun;
}

static item make_loaded_stanag()
{
    item mag( itype_stanag30 );
    mag.put_in( item( itype_556, calendar::turn, 30 ), pocket_type::MAGAZINE );
    return mag;
}

static item make_dual_well_gun()
{
    item gun( itype_test_multimag_gun );
    item glock_mag( itype_glockmag );
    glock_mag.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    gun.put_in( glock_mag, pocket_type::MAGAZINE_WELL );
    gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );
    return gun;
}

TEST_CASE( "single_well_magazine_operations", "[multimag]" )
{
    SECTION( "ammo_remaining and magazine_current" ) {
        item gun = make_loaded_glock();

        CHECK( gun.ammo_remaining() == 15 );
        REQUIRE( gun.magazine_current() != nullptr );
        CHECK( gun.magazine_current()->typeId() == itype_glockmag );
    }

    SECTION( "ammo_consume drains loaded magazine" ) {
        clear_map();
        map &here = get_map();
        tripoint_bub_ms pos( tripoint_bub_ms::zero );
        item gun = make_loaded_glock();

        CHECK( gun.ammo_consume( 3, here, pos, nullptr ) == 3 );
        CHECK( gun.ammo_remaining() == 12 );
        CHECK( gun.ammo_consume( 12, here, pos, nullptr ) == 12 );
        CHECK( gun.ammo_remaining() == 0 );
    }

    SECTION( "first_ammo returns loaded ammo type" ) {
        item gun = make_loaded_glock();
        CHECK( gun.first_ammo().typeId() == itype_9mm );
    }

    SECTION( "first_ammo on empty gun" ) {
        item gun( itype_glock_19 );
        CHECK( gun.first_ammo().is_null() );
    }

    SECTION( "magazines_current" ) {
        item gun = make_loaded_glock();
        std::vector<item *> mags = gun.magazines_current();
        REQUIRE( mags.size() == 1 );
        CHECK( mags[0] == gun.magazine_current() );

        item empty_gun( itype_glock_19 );
        CHECK( empty_gun.magazines_current().empty() );
    }
}

TEST_CASE( "integral_magazine_ammo_consume", "[multimag]" )
{
    clear_map();
    map &here = get_map();
    tripoint_bub_ms pos( tripoint_bub_ms::zero );

    item gun( itype_sw_619 );
    gun.put_in( item( itype_38_special, calendar::turn, 7 ), pocket_type::MAGAZINE );

    CHECK( gun.ammo_remaining() == 7 );
    CHECK( gun.ammo_consume( 3, here, pos, nullptr ) == 3 );
    CHECK( gun.ammo_remaining() == 4 );
    CHECK( gun.first_ammo().typeId() == itype_38_special );
}

TEST_CASE( "dual_well_magazine_operations", "[multimag]" )
{
    SECTION( "magazines_current with both loaded" ) {
        item gun = make_dual_well_gun();
        CHECK( gun.magazines_current().size() == 2 );
    }

    SECTION( "magazines_current with only second loaded" ) {
        item gun( itype_test_multimag_gun );
        gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );

        std::vector<item *> mags = gun.magazines_current();
        REQUIRE( mags.size() == 1 );
        REQUIRE( gun.magazine_current() != nullptr );
        CHECK( mags[0] == gun.magazine_current() );
    }

    SECTION( "ammo_consume with first well empty" ) {
        clear_map();
        map &here = get_map();
        tripoint_bub_ms pos( tripoint_bub_ms::zero );

        item gun( itype_test_multimag_gun );
        gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );

        CHECK( gun.ammo_remaining() == 30 );
        CHECK( gun.ammo_consume( 10, here, pos, nullptr ) == 10 );
        CHECK( gun.ammo_remaining() == 20 );
    }

    SECTION( "ammo_consume drains both wells" ) {
        clear_map();
        map &here = get_map();
        tripoint_bub_ms pos( tripoint_bub_ms::zero );

        item gun = make_dual_well_gun();

        CHECK( gun.ammo_consume( 20, here, pos, nullptr ) == 20 );
        std::vector<item *> remaining = gun.magazines_current();
        REQUIRE( remaining.size() == 2 );
        CHECK( remaining[0]->ammo_remaining() == 0 );
        CHECK( remaining[1]->ammo_remaining() == 25 );
    }

    SECTION( "first_ammo with first well empty" ) {
        item gun( itype_test_multimag_gun );
        gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );
        CHECK( gun.first_ammo().typeId() == itype_556 );
    }

    SECTION( "first_ammo with both wells empty" ) {
        item gun( itype_test_multimag_gun );
        CHECK( gun.first_ammo().is_null() );
    }
}

TEST_CASE( "rate_action_unload across wells", "[multimag][hint]" )
{
    clear_avatar();
    Character &you = get_player_character();

    SECTION( "good when only second well has a mag" ) {
        item gun( itype_test_multimag_gun );
        // Insert into the stanag (second) well only.
        gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );
        REQUIRE( gun.magazines_current().size() == 1 );
        CHECK( you.rate_action_unload( gun ) == hint_rating::good );
    }

    SECTION( "good when both wells loaded" ) {
        item gun = make_dual_well_gun();
        CHECK( you.rate_action_unload( gun ) == hint_rating::good );
    }

    SECTION( "iffy when no wells loaded but item has ammo capacity" ) {
        item gun( itype_test_multimag_gun );
        CHECK( you.rate_action_unload( gun ) == hint_rating::iffy );
    }
}

TEST_CASE( "display_name multi-well per-well counts", "[multimag][display]" )
{
    SECTION( "single loaded well prints (amount/max ammo) once" ) {
        item gun = make_loaded_glock();
        const std::string name = remove_color_tags( gun.display_name() );
        CAPTURE( name );
        CHECK( name.find( "(15/15" ) != std::string::npos );
    }

    SECTION( "dual loaded wells: per-well counts in single paren" ) {
        item gun = make_dual_well_gun();
        const std::string name = remove_color_tags( gun.display_name() );
        CAPTURE( name );
        // Both well counts appear, comma-separated, inside one outer paren.
        const size_t open = name.find( '(' );
        const size_t close = name.find( ')', open );
        REQUIRE( open != std::string::npos );
        REQUIRE( close != std::string::npos );
        const std::string inner = name.substr( open + 1, close - open - 1 );
        CAPTURE( inner );
        CHECK( inner.find( "15/15" ) != std::string::npos );
        CHECK( inner.find( "30/30" ) != std::string::npos );
        CHECK( inner.find( "," ) != std::string::npos );
    }

    SECTION( "one well loaded out of two still emits per-well segments" ) {
        override_option opt( "AMMO_IN_NAMES", "false" );
        item gun( itype_test_multimag_gun );
        gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );
        const std::string name = remove_color_tags( gun.display_name() );
        CAPTURE( name );
        const size_t open = name.find( '(' );
        const size_t close = name.find( ')', open );
        REQUIRE( open != std::string::npos );
        REQUIRE( close != std::string::npos );
        const std::string inner = name.substr( open + 1, close - open - 1 );
        CHECK( inner == "0/15, 30/30" );
    }

    SECTION( "dual loaded wells use variant ammo names, not generic class names" ) {
        override_option opt( "AMMO_IN_NAMES", "true" );
        item gun = make_dual_well_gun();
        const std::string name = remove_color_tags( gun.display_name() );
        CAPTURE( name );
        // Variant nname (e.g. "9x19mm JHP") not generic ammotype name.
        const std::string variant_9mm = item::find_type( itype_9mm )->nname( 1 );
        const std::string variant_556 = item::find_type( itype_556 )->nname( 1 );
        CHECK( name.find( variant_9mm ) != std::string::npos );
        CHECK( name.find( variant_556 ) != std::string::npos );
    }

    SECTION( "AMMO_IN_NAMES off: dual wells print counts only, no ammo names" ) {
        override_option opt( "AMMO_IN_NAMES", "false" );
        item gun = make_dual_well_gun();
        const std::string name = remove_color_tags( gun.display_name() );
        CAPTURE( name );
        const size_t open = name.find( '(' );
        const size_t close = name.find( ')', open );
        REQUIRE( open != std::string::npos );
        REQUIRE( close != std::string::npos );
        const std::string inner = name.substr( open + 1, close - open - 1 );
        // Just "<a>/<b>, <c>/<d>" with no ammo names interleaved.
        CHECK( inner.find( item::find_type( itype_9mm )->nname( 1 ) ) == std::string::npos );
        CHECK( inner.find( item::find_type( itype_556 )->nname( 1 ) ) == std::string::npos );
        CHECK( inner.find( "15/15" ) != std::string::npos );
        CHECK( inner.find( "30/30" ) != std::string::npos );
    }
}

TEST_CASE( "iteminfo Magazine label pluralization", "[multimag][display]" )
{
    auto iteminfo_for = []( const item & it ) {
        std::vector<iteminfo> info_v;
        const std::vector<iteminfo_parts> parts = {
            iteminfo_parts::GUN_MAGAZINE,
            iteminfo_parts::TOOL_MAGAZINE_CURRENT
        };
        const iteminfo_query query_v( parts );
        it.info( info_v, &query_v, 1 );
        return format_item_info( info_v, {} );
    };

    SECTION( "single loaded: singular 'Magazine:' label" ) {
        item gun = make_loaded_glock();
        const std::string info = iteminfo_for( gun );
        CAPTURE( info );
        CHECK( info.find( "Magazine: " ) != std::string::npos );
        CHECK( info.find( "Magazines: " ) == std::string::npos );
        // Loaded magazine's tname is interpolated.
        CHECK( info.find( item( itype_glockmag ).tname() ) != std::string::npos );
    }

    SECTION( "dual loaded: plural 'Magazines:' label, both names listed" ) {
        item gun = make_dual_well_gun();
        const std::string info = iteminfo_for( gun );
        CAPTURE( info );
        CHECK( info.find( "Magazines: " ) != std::string::npos );
        const std::string glock_name = item( itype_glockmag ).tname();
        const std::string stanag_name = item( itype_stanag30 ).tname();
        CHECK( info.find( glock_name ) != std::string::npos );
        CHECK( info.find( stanag_name ) != std::string::npos );
        // The two names appear comma-separated in the same line.
        const size_t glock_pos = info.find( glock_name );
        const size_t stanag_pos = info.find( stanag_name );
        if( glock_pos != std::string::npos && stanag_pos != std::string::npos ) {
            const size_t lo = std::min( glock_pos, stanag_pos );
            const size_t hi = std::max( glock_pos, stanag_pos );
            CHECK( info.substr( lo, hi - lo ).find( "," ) != std::string::npos );
        }
    }

    SECTION( "single loaded tool: tool_info path uses singular 'Magazine:'" ) {
        item flashlight( itype_flashlight );
        flashlight.put_in( item( itype_medium_battery_cell ), pocket_type::MAGAZINE_WELL );
        const std::string info = iteminfo_for( flashlight );
        CAPTURE( info );
        CHECK( info.find( "Magazine: " ) != std::string::npos );
        CHECK( info.find( "Magazines: " ) == std::string::npos );
        CHECK( info.find( item( itype_medium_battery_cell ).tname() ) != std::string::npos );
    }
}

TEST_CASE( "per-well ammo_remaining / ammo_capacity / remaining_ammo_capacity overloads",
           "[multimag][capacity]" )
{
    item gun = make_dual_well_gun();
    REQUIRE( gun.magazines_current().size() == 2 );
    // Drain a few rounds out of the first well so the per-well numbers diverge
    // from the second well's full count.
    {
        clear_map();
        map &here = get_map();
        const tripoint_bub_ms pos( tripoint_bub_ms::zero );
        REQUIRE( gun.ammo_consume( 5, here, pos, nullptr ) == 5 );
    }

    // Compute well indices from the fixture; do not hardcode.
    int glock_idx = -1;
    int stanag_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                if( const item *mag = p->magazine_current() ) {
                    if( mag->typeId() == itype_glockmag ) {
                        glock_idx = idx;
                    } else if( mag->typeId() == itype_stanag30 ) {
                        stanag_idx = idx;
                    }
                }
            }
            ++idx;
        }
    }
    REQUIRE( glock_idx >= 0 );
    REQUIRE( stanag_idx >= 0 );

    SECTION( "per-well ammo_remaining returns each well's loaded count" ) {
        CHECK( gun.ammo_remaining( glock_idx ) == 10 );
        CHECK( gun.ammo_remaining( stanag_idx ) == 30 );
        // Aggregate is the sum.
        CHECK( gun.ammo_remaining() == 40 );
    }

    SECTION( "per-well ammo_capacity returns each loaded magazine's capacity" ) {
        CHECK( gun.ammo_capacity( glock_idx ) == 15 );
        CHECK( gun.ammo_capacity( stanag_idx ) == 30 );
    }

    SECTION( "per-well remaining_ammo_capacity is per-mag, not item-aggregate" ) {
        CHECK( gun.remaining_ammo_capacity( glock_idx ) == 5 );
        CHECK( gun.remaining_ammo_capacity( stanag_idx ) == 0 );
    }

    SECTION( "out-of-range or non-well indices return 0" ) {
        CHECK( gun.ammo_remaining( -1 ) == 0 );
        CHECK( gun.ammo_remaining( 999 ) == 0 );
        CHECK( gun.ammo_capacity( -1 ) == 0 );
        CHECK( gun.remaining_ammo_capacity( -1 ) == 0 );
    }
}

TEST_CASE( "Character::list_ammo per_well_targets flag", "[multimag][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    // Without storage, i_add silently drops items on the ground and find_ammo
    // would not see them.
    you.wear_item( item( itype_backpack ) );

    SECTION( "default flag emits one option per item plus one per loaded magazine" ) {
        item gun = make_dual_well_gun();
        item_location gun_loc = you.i_add( gun );
        REQUIRE( gun_loc );
        // Loose ammo so list_ammo has something beyond the loaded mags.
        you.i_add( item( itype_9mm, calendar::turn, 15 ) );
        you.i_add( item( itype_556, calendar::turn, 30 ) );

        std::vector<item::reload_option> ammo_list;
        you.list_ammo( gun_loc, ammo_list, /*empty=*/true );
        // Every emitted option must carry the first-compatible-well sentinel
        // when the per_well_targets flag is unset.
        for( const item::reload_option &opt : ammo_list ) {
            CHECK( opt.pocket_index < 0 );
        }
    }

    SECTION( "per_well_targets=true emits per-well options with stable pocket_index" ) {
        // Use an empty multi-well gun so find_ammo will accept the spare
        // magazines as valid reload candidates (find_ammo rejects spares
        // when the well is already full and now=true).
        item gun( itype_test_multimag_gun );
        item_location gun_loc = you.i_add( gun );
        REQUIRE( gun_loc );
        item glock_extra( itype_glockmag );
        glock_extra.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
        item stanag_extra( itype_stanag30 );
        stanag_extra.put_in( item( itype_556, calendar::turn, 30 ), pocket_type::MAGAZINE );
        you.i_add( glock_extra );
        you.i_add( stanag_extra );

        // Collect the two well indices in gun_loc->contents.
        std::vector<int> well_indices;
        {
            int idx = 0;
            for( const item_pocket *p : gun_loc->get_pockets(
            []( const item_pocket & ) {
            return true;
        } ) ) {
                if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                    well_indices.push_back( idx );
                }
                ++idx;
            }
        }
        REQUIRE( well_indices.size() == 2 );

        std::vector<item::reload_option> ammo_list;
        you.list_ammo( gun_loc, ammo_list, /*empty=*/true, /*per_well_targets=*/true );

        std::set<int> emitted_pocket_indices;
        for( const item::reload_option &opt : ammo_list ) {
            emitted_pocket_indices.insert( opt.pocket_index );
        }
        // Both wells emit at their contents index.
        for( int idx : well_indices ) {
            CHECK( emitted_pocket_indices.count( idx ) == 1 );
        }

        // No cross-well leak: a glockmag option must not target the stanag well.
        const item_pocket *first_well = nullptr;
        const item_pocket *second_well = nullptr;
        {
            int idx = 0;
            for( const item_pocket *p : gun_loc->get_pockets(
            []( const item_pocket & ) {
            return true;
        } ) ) {
                if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                    if( idx == well_indices[0] ) {
                        first_well = p;
                    } else if( idx == well_indices[1] ) {
                        second_well = p;
                    }
                }
                ++idx;
            }
        }
        REQUIRE( first_well != nullptr );
        REQUIRE( second_well != nullptr );
        for( const item::reload_option &opt : ammo_list ) {
            if( opt.pocket_index < 0 ) {
                continue;
            }
            const item_pocket *picked = opt.pocket_index == well_indices[0] ? first_well : second_well;
            CAPTURE( opt.pocket_index );
            CAPTURE( opt.ammo->tname() );
            CHECK( picked->can_reload_with( *opt.ammo, true ) );
        }

        // Class-(a) entries swap one magazine: qty() == 1.
        for( const item::reload_option &opt : ammo_list ) {
            if( opt.pocket_index >= 0 ) {
                CHECK( opt.qty() == 1 );
            }
        }
    }

    SECTION( "empty-well pocket_index is the position in contents, not a "
             "magazines_current() position" ) {
        // Indices must match contents, not magazines_current().
        item gun( itype_test_multimag_gun );
        gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );
        item_location gun_loc = you.i_add( gun );
        REQUIRE( gun_loc );

        item glock_extra( itype_glockmag );
        glock_extra.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
        item stanag_extra( itype_stanag30 );
        stanag_extra.put_in( item( itype_556, calendar::turn, 30 ), pocket_type::MAGAZINE );
        you.i_add( glock_extra );
        you.i_add( stanag_extra );

        // Locate each well in gun_loc->contents.
        int empty_well_idx = -1;
        int loaded_well_idx = -1;
        {
            int idx = 0;
            for( const item_pocket *p : gun_loc->get_pockets(
            []( const item_pocket & ) {
            return true;
        } ) ) {
                if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                    if( p->magazine_current() == nullptr ) {
                        empty_well_idx = idx;
                    } else {
                        loaded_well_idx = idx;
                    }
                }
                ++idx;
            }
        }
        REQUIRE( empty_well_idx >= 0 );
        REQUIRE( loaded_well_idx >= 0 );
        REQUIRE( empty_well_idx != loaded_well_idx );

        std::vector<item::reload_option> ammo_list;
        you.list_ammo( gun_loc, ammo_list, /*empty=*/true, /*per_well_targets=*/true );

        // Empty well still addressable by its stable contents index.
        bool saw_empty = false;
        for( const item::reload_option &opt : ammo_list ) {
            if( opt.pocket_index == empty_well_idx ) {
                saw_empty = true;
            }
        }
        CHECK( saw_empty );
    }
}

TEST_CASE( "item::reload with pocket_index targets a specific well", "[multimag][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item( itype_backpack ) );

    // Multi-well gun with only the first (glock) well loaded; the stanag
    // well stays empty so item::reload(stanag_idx) has somewhere to insert.
    item gun( itype_test_multimag_gun );
    item glock_mag( itype_glockmag );
    glock_mag.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    gun.put_in( glock_mag, pocket_type::MAGAZINE_WELL );

    item_location gun_loc = you.i_add( gun );
    REQUIRE( gun_loc );

    // Identify the two well indices (glock-loaded and empty-stanag) in
    // gun_loc->contents.
    int loaded_well_idx = -1;
    int empty_well_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun_loc->get_pockets(
        []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                if( p->magazine_current() != nullptr ) {
                    loaded_well_idx = idx;
                } else {
                    empty_well_idx = idx;
                }
            }
            ++idx;
        }
    }
    REQUIRE( loaded_well_idx >= 0 );
    REQUIRE( empty_well_idx >= 0 );

    SECTION( "pocket_index = empty stanag well inserts there, leaves glock untouched" ) {
        item replacement_stanag( itype_stanag30 );
        replacement_stanag.put_in( item( itype_556, calendar::turn, 10 ), pocket_type::MAGAZINE );
        item_location replacement_loc = you.i_add( replacement_stanag );
        REQUIRE( replacement_loc );

        const int glock_count_before = gun_loc->ammo_remaining( loaded_well_idx );
        REQUIRE( gun_loc->reload( you, replacement_loc, 1, empty_well_idx ) );
        CHECK( gun_loc->ammo_remaining( empty_well_idx ) == 10 );
        CHECK( gun_loc->ammo_remaining( loaded_well_idx ) == glock_count_before );
    }

    SECTION( "invalid pocket_index aborts reload" ) {
        item replacement_stanag( itype_stanag30 );
        replacement_stanag.put_in( item( itype_556, calendar::turn, 10 ), pocket_type::MAGAZINE );
        item_location replacement_loc = you.i_add( replacement_stanag );
        REQUIRE( replacement_loc );

        const int glock_count_before = gun_loc->ammo_remaining( loaded_well_idx );
        bool reload_result = true;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            // Out-of-range index. Aborts with a debugmsg, which we capture
            // so it does not flag the test as failed.
            reload_result = gun_loc->reload( you, replacement_loc, 1, 99 );
        } );
        CHECK( !reload_result );
        CHECK( dmsg.find( "pocket_index" ) != std::string::npos );
        CHECK( gun_loc->ammo_remaining( loaded_well_idx ) == glock_count_before );
        CHECK( gun_loc->ammo_remaining( empty_well_idx ) == 0 );
    }
}

TEST_CASE( "reload_option::qty forces 1 for pocket-targeted (class a) reloads",
           "[multimag][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item( itype_backpack ) );

    item gun = make_dual_well_gun();
    item_location gun_loc = you.i_add( gun );
    REQUIRE( gun_loc );

    // A whole stack of replacement glock magazines so available_ammo > 1.
    item glock_extra( itype_glockmag );
    glock_extra.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    item_location mag_loc = you.i_add( glock_extra );
    REQUIRE( mag_loc );
    you.i_add( glock_extra );
    you.i_add( glock_extra );

    int glock_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun_loc->get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                if( const item *mag = p->magazine_current() ) {
                    if( mag->typeId() == itype_glockmag ) {
                        glock_idx = idx;
                        break;
                    }
                }
            }
            ++idx;
        }
    }
    REQUIRE( glock_idx >= 0 );

    item::reload_option opt( &you, gun_loc, mag_loc );
    opt.pocket_index = glock_idx;
    opt.qty( 1000 );
    CHECK( opt.qty() == 1 );
}

TEST_CASE( "get_possible_reload_targets emits per-well reload_target entries",
           "[multimag][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item( itype_backpack ) );

    SECTION( "multi-well gun: one well entry per MAGAZINE_WELL plus loaded-mag entry" ) {
        item gun = make_dual_well_gun();
        item_location gun_loc = you.i_add( gun );
        REQUIRE( gun_loc );

        const std::vector<reload_target> targets = get_possible_reload_targets( gun_loc );
        std::set<int> well_indices;
        int loaded_mag_count = 0;
        for( const reload_target &rt : targets ) {
            if( rt.kind == reload_target::kind::well && rt.owner == gun_loc ) {
                well_indices.insert( rt.pocket_index );
            } else if( rt.kind == reload_target::kind::loaded_mag && rt.owner == gun_loc ) {
                loaded_mag_count++;
            }
        }
        // Both wells appear with their actual contents indices, and each
        // loaded magazine produces its own loaded-mag entry.
        CHECK( well_indices.size() == 2 );
        CHECK( loaded_mag_count == 2 );
    }

    SECTION( "integral-mag gun emits a fallback loaded-mag entry on the gun itself" ) {
        item revolver( itype_sw_619 );
        item_location revolver_loc = you.i_add( revolver );
        REQUIRE( revolver_loc );

        const std::vector<reload_target> targets = get_possible_reload_targets( revolver_loc );
        bool found_fallback = false;
        for( const reload_target &rt : targets ) {
            if( rt.kind == reload_target::kind::loaded_mag && rt.target == revolver_loc &&
                rt.pocket_index < 0 ) {
                found_fallback = true;
            }
        }
        CHECK( found_fallback );
    }
}

TEST_CASE( "find_matching_reload_target routes ammo to the correct well",
           "[multimag][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item( itype_backpack ) );

    item gun( itype_test_multimag_gun );
    item_location gun_loc = you.i_add( gun );
    REQUIRE( gun_loc );

    const std::vector<reload_target> targets = get_possible_reload_targets( gun_loc );

    // Find each well's contents index in gun_loc.
    int glock_well_idx = -1;
    int stanag_well_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun_loc->get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                if( glock_well_idx < 0 ) {
                    glock_well_idx = idx;
                } else if( stanag_well_idx < 0 ) {
                    stanag_well_idx = idx;
                }
            }
            ++idx;
        }
    }
    REQUIRE( glock_well_idx >= 0 );
    REQUIRE( stanag_well_idx >= 0 );

    SECTION( "glock magazine routes to the glockmag-restricted well" ) {
        item glock_mag( itype_glockmag );
        glock_mag.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
        item_location glock_loc = you.i_add( glock_mag );
        REQUIRE( glock_loc );

        const reload_target *match = find_matching_reload_target( targets, glock_loc );
        REQUIRE( match != nullptr );
        CHECK( match->kind == reload_target::kind::well );
        CHECK( match->pocket_index == glock_well_idx );
    }

    SECTION( "stanag magazine routes to the stanag30-restricted well" ) {
        item stanag_mag( itype_stanag30 );
        stanag_mag.put_in( item( itype_556, calendar::turn, 30 ), pocket_type::MAGAZINE );
        item_location stanag_loc = you.i_add( stanag_mag );
        REQUIRE( stanag_loc );

        const reload_target *match = find_matching_reload_target( targets, stanag_loc );
        REQUIRE( match != nullptr );
        CHECK( match->kind == reload_target::kind::well );
        CHECK( match->pocket_index == stanag_well_idx );
    }

    SECTION( "same-type dual-well: find_all_matching emits one entry per accepting well" ) {
        item same_type_gun( itype_test_multimag_gun_same_type );
        item_location same_loc = you.i_add( same_type_gun );
        REQUIRE( same_loc );
        const std::vector<reload_target> same_targets = get_possible_reload_targets( same_loc );

        item glock_mag( itype_glockmag );
        glock_mag.put_in( item( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
        item_location glock_loc = you.i_add( glock_mag );
        REQUIRE( glock_loc );

        const std::vector<const reload_target *> matches =
            find_all_matching_reload_targets( same_targets, glock_loc );
        REQUIRE( matches.size() == 2 );
        // Class-(a) well entries with distinct pocket_index for disambiguation.
        std::set<int> seen_pocket_indices;
        for( const reload_target *rt : matches ) {
            CHECK( rt->kind == reload_target::kind::well );
            seen_pocket_indices.insert( rt->pocket_index );
        }
        CHECK( seen_pocket_indices.size() == 2 );
    }

    SECTION( "same-type loaded mags: loose ammo matches every loaded mag for class-(b) top-up" ) {
        // Both wells loaded with glockmag; one half-full, one nearly empty.
        // Direct-insert into each well by walking pockets; put_in finds only
        // the first matching pocket and would route both magazines into well
        // 1 here.
        item gun_two( itype_test_multimag_gun_same_type );
        item glock_a( itype_glockmag );
        glock_a.put_in( item( itype_9mm, calendar::turn, 5 ), pocket_type::MAGAZINE );
        item glock_b( itype_glockmag );
        glock_b.put_in( item( itype_9mm, calendar::turn, 1 ), pocket_type::MAGAZINE );
        std::vector<item_pocket *> wells;
        for( item_pocket *p : gun_two.get_pockets( []( const item_pocket & pp ) {
        return pp.is_type( pocket_type::MAGAZINE_WELL );
        } ) ) {
            wells.push_back( p );
        }
        REQUIRE( wells.size() == 2 );
        REQUIRE( wells[0]->insert_item( glock_a ).success() );
        REQUIRE( wells[1]->insert_item( glock_b ).success() );
        REQUIRE( gun_two.magazines_current().size() == 2 );

        item_location two_loc = you.i_add( gun_two );
        REQUIRE( two_loc );
        REQUIRE( two_loc->magazines_current().size() == 2 );

        const std::vector<reload_target> two_targets = get_possible_reload_targets( two_loc );

        item loose_9mm( itype_9mm, calendar::turn, 30 );
        item_location loose_loc = you.i_add( loose_9mm );
        REQUIRE( loose_loc );

        const std::vector<const reload_target *> matches =
            find_all_matching_reload_targets( two_targets, loose_loc );
        // Both loaded magazines accept loose 9mm; the helper must surface
        // both so the disambiguation menu can offer a real choice.
        int loaded_mag_matches = 0;
        std::vector<int> loaded_remaining;
        for( const reload_target *rt : matches ) {
            if( rt->kind == reload_target::kind::loaded_mag ) {
                loaded_mag_matches++;
                loaded_remaining.push_back( rt->target->ammo_remaining() );
            }
        }
        CHECK( loaded_mag_matches == 2 );

        // Picking the less-full mag must let qty() refill toward its own
        // remaining capacity, not stay capped by the more-full mag.
        const reload_target *small_mag_target = nullptr;
        for( const reload_target *rt : matches ) {
            if( rt->kind == reload_target::kind::loaded_mag &&
                rt->target->ammo_remaining() == 1 ) {
                small_mag_target = rt;
                break;
            }
        }
        REQUIRE( small_mag_target != nullptr );
        item::reload_option opt( &you, small_mag_target->target, loose_loc,
                                 small_mag_target->pocket_index );
        // glockmag capacity 15, mag has 1 round, room for 14.
        CHECK( opt.qty() == 14 );

        // Player picked a partial count below either mag's remaining capacity;
        // disambiguation must preserve it.
        item::reload_option opt_partial( &you, small_mag_target->target, loose_loc,
                                         small_mag_target->pocket_index );
        opt_partial.qty( 3 );
        CHECK( opt_partial.qty() == 3 );
    }

    SECTION( "magazine compatible with no well returns nullptr" ) {
        // 38_special is loose ammo for the integral revolver fixture and is
        // not compatible with either of test_multimag_gun's wells (glockmag
        // / stanag30 restrictions). Loose ammo cannot reload a well.
        item loose( itype_38_special, calendar::turn, 7 );
        item_location loose_loc = you.i_add( loose );
        REQUIRE( loose_loc );

        const reload_target *match = find_matching_reload_target( targets, loose_loc );
        CHECK( match == nullptr );
    }
}

TEST_CASE( "Character::unload ejects every loaded magazine", "[multimag][unload]" )
{
    clear_avatar();
    clear_map();
    Character &you = get_player_character();
    you.wear_item( item( itype_backpack ) );

    item gun = make_dual_well_gun();
    REQUIRE( gun.magazines_current().size() == 2 );

    item_location gun_loc = you.i_add( gun );
    REQUIRE( gun_loc );

    const bool unloaded = you.unload( gun_loc, /*bypass_activity=*/true );
    CHECK( unloaded );

    const std::vector<item *> remaining = gun_loc->magazines_current();
    CHECK( remaining.empty() );

    bool found_glockmag = false;
    bool found_stanag = false;
    you.visit_items( [&]( const item * it, item * ) {
        if( it->typeId() == itype_glockmag ) {
            found_glockmag = true;
        }
        if( it->typeId() == itype_stanag30 ) {
            found_stanag = true;
        }
        return VisitResponse::NEXT;
    } );
    CHECK( found_glockmag );
    CHECK( found_stanag );
}

TEST_CASE( "same-type dual-well: targeted reload replaces a specific well",
           "[multimag][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item( itype_backpack ) );

    item gun( itype_test_multimag_gun_same_type );
    item glock_a( itype_glockmag );
    glock_a.ammo_set( itype_9mm, 15 );
    item glock_b( itype_glockmag );
    glock_b.ammo_set( itype_9mm, 5 );

    std::vector<int> well_indices;
    {
        int idx = 0;
        for( const item_pocket *p : gun.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                well_indices.push_back( idx );
            }
            ++idx;
        }
    }
    REQUIRE( well_indices.size() == 2 );

    std::vector<item_pocket *> wells = gun.get_pockets( []( const item_pocket & p ) {
        return p.is_type( pocket_type::MAGAZINE_WELL );
    } );
    REQUIRE( wells.size() == 2 );
    REQUIRE( wells[0]->insert_item( glock_a ).success() );
    REQUIRE( wells[1]->insert_item( glock_b ).success() );

    item_location gun_loc = you.i_add( gun );
    REQUIRE( gun_loc );

    item replacement( itype_glockmag );
    replacement.ammo_set( itype_9mm, 15 );
    item_location replacement_loc = you.i_add( replacement );
    REQUIRE( replacement_loc );

    const int target_well = well_indices[1];
    const bool ok = gun_loc->reload( you, replacement_loc, 1, target_well );
    REQUIRE( ok );

    std::vector<const item_pocket *> wells_after = std::as_const( *gun_loc ).get_pockets(
    []( const item_pocket & p ) {
        return p.is_type( pocket_type::MAGAZINE_WELL );
    } );
    REQUIRE( wells_after.size() == 2 );
    REQUIRE( wells_after[0]->magazine_current() != nullptr );
    REQUIRE( wells_after[1]->magazine_current() != nullptr );
    CHECK( wells_after[0]->magazine_current()->ammo_remaining() == 15 );
    CHECK( wells_after[1]->magazine_current()->ammo_remaining() == 15 );
}

TEST_CASE( "reload_activity_actor serializes pocket_index",
           "[multimag][reload][serialize]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item( itype_backpack ) );

    item gun = make_dual_well_gun();
    item_location gun_loc = you.i_add( gun );
    REQUIRE( gun_loc );
    item ammo( itype_glockmag );
    ammo.ammo_set( itype_9mm, 15 );
    item_location ammo_loc = you.i_add( ammo );
    REQUIRE( ammo_loc );

    item::reload_option opt( &you, gun_loc, ammo_loc, /*pocket_index=*/0 );
    reload_activity_actor actor( std::move( opt ) );

    std::ostringstream oss;
    JsonOut jsout( oss );
    actor.serialize( jsout );
    const std::string serialized = oss.str();
    CAPTURE( serialized );
    CHECK( serialized.find( "\"pocket_index\":0" ) != std::string::npos );
}

TEST_CASE( "wield-collision sees mags from every well, not just the first",
           "[multimag][wield]" )
{
    item gun = make_dual_well_gun();
    const std::vector<item *> mags = gun.magazines_current();
    REQUIRE( mags.size() == 2 );
    std::set<itype_id> mag_types;
    for( const item *m : mags ) {
        mag_types.insert( m->typeId() );
    }
    CHECK( mag_types.count( itype_glockmag ) == 1 );
    CHECK( mag_types.count( itype_stanag30 ) == 1 );
}
