#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "item.h"
#include "item_location.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "map.h"
#include "map_helpers.h"
#include "options_helpers.h"
#include "output.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "ret_val.h"
#include "type_id.h"

static const itype_id itype_38_special( "38_special" );
static const itype_id itype_556( "556" );
static const itype_id itype_9mm( "9mm" );
static const itype_id itype_flashlight( "flashlight" );
static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_medium_battery_cell( "medium_battery_cell" );
static const itype_id itype_stanag30( "stanag30" );
static const itype_id itype_sw_619( "sw_619" );
static const itype_id itype_test_multimag_gun( "test_multimag_gun" );

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
        item gun( itype_test_multimag_gun );
        gun.put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );
        const std::string name = remove_color_tags( gun.display_name() );
        CAPTURE( name );
        // Loaded stanag well still shows full count; empty glock well shows 0/cap.
        CHECK( name.find( "30/30" ) != std::string::npos );
        CHECK( name.find( "0/15" ) != std::string::npos );
        const size_t open = name.find( '(' );
        const size_t close = name.find( ')', open );
        REQUIRE( open != std::string::npos );
        REQUIRE( close != std::string::npos );
        const std::string inner = name.substr( open + 1, close - open - 1 );
        CHECK( inner.find( "," ) != std::string::npos );
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

TEST_CASE( "Character::unload ejects every loaded magazine", "[multimag][unload]" )
{
    clear_avatar();
    clear_map();
    Character &you = get_player_character();

    item gun = make_dual_well_gun();
    REQUIRE( gun.magazines_current().size() == 2 );

    item_location gun_loc = you.i_add( gun );
    REQUIRE( gun_loc );

    const bool unloaded = you.unload( gun_loc, /*bypass_activity=*/true );
    CHECK( unloaded );

    const std::vector<item *> remaining = gun_loc->magazines_current();
    CHECK( remaining.empty() );
}
