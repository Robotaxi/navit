/* prototypes */

/* common */
ATTR2(0x00000000,none)
ATTR(any)

ATTR2(0x00010000,type_item_begin)
ATTR(town_streets_item)
ATTR(street_name_item)
ATTR(street_name_numbers_item)
ATTR(street_item)
ATTR(street_number_item)
ATTR(item_type) /* fixme */
ATTR2(0x0001ffff,type_item_end)

ATTR2(0x00020000,type_int_begin)
ATTR(h)
ATTR(id)
ATTR(flags)
ATTR(w)
ATTR(x)
ATTR(y)
ATTR(flush_size)
ATTR(flush_time)
ATTR(zipfile_ref)
ATTR(country_id)
ATTR(position_sats)
ATTR(position_sats_used)
ATTR(update)
ATTR(follow)
ATTR(length)
ATTR(time)
ATTR(destination_length)
ATTR(destination_time)
ATTR(speed)
ATTR(interval)
ATTR(position_qual)
ATTR(zoom)
ATTR(retry_interval)
ATTR(projection)
ATTR(offroad)
ATTR(vocabulary_name)
ATTR(vocabulary_name_systematic)
ATTR(vocabulary_distances)
ATTR(announce_name_systematic_first)
ATTR2(0x00028000,type_boolean_begin)
/* boolean */
ATTR(overwrite)
ATTR(active)
ATTR(cursor)
ATTR(orientation)
ATTR(tracking)
ATTR(menubar)
ATTR(statusbar)
ATTR(toolbar)
ATTR(animate)
ATTR(lazy)
ATTR(mkdir)
ATTR2(0x0002ffff,type_int_end)
ATTR2(0x00030000,type_string_begin)
ATTR(type)
ATTR(label)
ATTR(data)
ATTR(charset)
ATTR(country_all)
ATTR(country_iso3)
ATTR(country_iso2)
ATTR(country_car)
ATTR(country_name)
ATTR(town_name)
ATTR(town_postal)
ATTR(district_name)
ATTR(street_name)
ATTR(street_name_systematic)
ATTR(street_number)
ATTR(debug)
ATTR(address)
ATTR(phone)
ATTR(entry_fee)
ATTR(open_hours)
ATTR(skin)
ATTR(fullscreen)
ATTR(view_mode)
ATTR(tilt)
ATTR(media_window_title)
ATTR(media_cmd)
ATTR(image_codec)
/* poi */
ATTR(icon)
ATTR(info_html)
ATTR(price_html)
/* navigation */
ATTR(navigation_short)
ATTR(navigation_long)
ATTR(navigation_long_exact)
ATTR(navigation_speech)
ATTR(name)
ATTR(source)
ATTR(description)
ATTR(gc_type)
ATTR(layout)
ATTR(position_nmea)
ATTR(gpsd_query)
ATTR(on_eof)
ATTR2(0x0003ffff,type_string_end)
ATTR(order_limit)
ATTR2(0x00050000,type_double_start)
ATTR(position_height)
ATTR(position_speed)
ATTR(position_direction)
ATTR2(0x0005ffff,type_double_end)
ATTR2(0x00060000,type_coord_geo_start)
ATTR(position_coord_geo)
ATTR(center)
ATTR2(0x0006ffff,type_coord_geo_end)
ATTR2(0x00070000,type_color_begin)
ATTR(color)
ATTR(color2)
ATTR2(0x0007ffff,type_color_end)
ATTR2(0x00080000,type_object_begin)
ATTR(navit)
ATTR(log)
ATTR(callback)
ATTR(route)
ATTR(navigation)
ATTR(vehicle)
ATTR(map)
ATTR(bookmark_map)
ATTR(former_destination_map)
ATTR(graphics)
ATTR(gui)
ATTR2(0x0008ffff,type_object_end)
ATTR2(0x00090000,type_coord_begin)
ATTR2(0x0009ffff,type_coord_end)
ATTR2(0x000a0000,type_pcoord_begin)
ATTR(destination)
ATTR2(0x000affff,type_pcoord_end)
