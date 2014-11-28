/* some changes in navigation.c
 *
 * done : teach Navit the difference between straight and turn, solves
 * 		many many false 'go right' or whatever instructions.
 * done :Annouces a "merge" onto the higway, and since we now actually
 * 		maneuvre onto the highway, it's name becomes available in osd items
 * done : Navit now can distinguish an exit from a ramp(leading to some....)
 * done : improve announcements in dutch (#1274 annoying 'into the street')
 *
 * todo : investigate cases where a ramp leads to a higwhay and
 * 		gives a chance to merge but also continue on the ramp to merge
 * 		to something else further on. (low priority)
 *
 *
 *
 *
 * some of the above relate to (partially or in whole)
 * #1265 from mvglaslow
 * #1271 from jandegr
 * #1274 from jandegr
 * #1174 from arnaud le meur
 * #1082 from robotaxi
 * #921 from psoding
 * #795 from user:ps333
 * #694 from user:nop
 * #660 from user:polarbear_n
 *
 * and an incomplete list of more navigation.c related tickets
 * #1190
 * #1160
 * #1161
 * #1095
 * #1087
 * #880
 * #870
 * (#519)
 *
 */


/**
 * Navit, a modular navigation system.
 * Copyright (C) 2005-20014 Navit Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <glib.h>
#include "debug.h"
#include "profile.h"
#include "navigation.h"
#include "coord.h"
#include "item.h"
#include "xmlconfig.h"
#include "route.h"
#include "transform.h"
#include "mapset.h"
#include "projection.h"
#include "map.h"
#include "navit.h"
#include "callback.h"
#include "speech.h"
#include "vehicleprofile.h"
#include "plugin.h"
#include "navit_nls.h"
#include "util.h"

/* #define DEBUG */

/* TODO: these are transitional constants to allow using the new (r5960) logging code
 * prior to merging. Remove these definitions when merging with r5960 or later.
 */
/** Internal use only, do not use for logging. */
#define lvl_unset -1
/** Error: something did not work. */
#define lvl_error 0
/** Warning: something may not have worked. */
#define lvl_warning 1
/** Informational message. Should make sense to non-programmers. */
#define lvl_info 2
/** Debug output: (almost) anything goes. */
#define lvl_debug 3
/* end transitionsal constants */


static int roundabout_extra_length=50;
static int angle_straight = 2;	/* turns with -angle_straight <= delta <= angle_straight
								 * will be seen as going straight.
								 *
								 * Use a really narrow gap here, fixes already a large number
								 * of false commands without causing other problems.
								 *
								 * During testing it became clear that widening the gap reduces
								 * even more unwanted 'go right or left' but soon starts to show side-effects.
								 *
								 * maybe think of a better name some day.
								 *
								 */


/* quick 'fixes it for me in dutch' see #1274
 * and has a medium to low priority,
 * but try to use nice-names and more constants
 * right from the start and all-over the place.
 *
 * I also have a vague impression that mixed-case
 * is not always hanled well, low priority but I make a note
 * of it anyway.
 *
 */
struct suffix {
	enum gender {unknown, male, female, neutral};
	char *fullname;
	char *abbrev;
	int sex;
} suffixes[]= {
	{"weg",NULL,male},
	{"platz","pl.",male},
	{"ring",NULL,male},
	{"allee",NULL,female},
	{"gasse",NULL,female},
	{"straße","str.",female},

	/* some for the dutch lang. */
	{"straat",NULL,neutral},
	{"weg",NULL,neutral},
	{"baan",NULL,neutral},
	{"laan",NULL,neutral},
	{"wegel",NULL,neutral},

	/* some for the french lang. */
	{"boulevard",NULL,male},
	{"avenue",NULL,female},
	{"chemin",NULL,neutral},
	{"rue",NULL,female},

	/* some for the english lang. */
	{"street",NULL,male},
/*	{"avenue",NULL,female}, doubles up with french, not sure what to do in such cases */
/*	{"boulevard",NULL,male}, likewise doubles up*/
	{"drive",NULL,male},

};





struct navigation {
	NAVIT_OBJECT
	struct route *route;
	struct map *map;
	struct item_hash *hash;
	struct vehicleprofile *vehicleprofile;
	struct navigation_itm *first;
	struct navigation_itm *last;
	struct navigation_command *cmd_first;
	struct navigation_command *cmd_last;
	struct callback_list *callback_speech;
	struct callback_list *callback;
	struct navit *navit;
	struct speech *speech;
	int level_last;
	struct item item_last;
	int turn_around;
	int turn_around_limit;
	int distance_turn;
	struct callback *route_cb;
	int announce[route_item_last-route_item_first+1][3];
	int tell_street_name;
	int delay;
	int curr_delay;
	int turn_around_count;
	int flags;
};

int distances[]={1,2,3,4,5,10,25,50,75,100,150,200,250,300,400,500,750,-1};


struct navigation_command {
	struct navigation_itm *itm;
	struct navigation_command *next;
	struct navigation_command *prev;
	int delta;
	int roundabout_delta;
	int length;
	char * reason;
};

/**
 * @brief Holds a way that one could possibly drive from a navigation item
 *
 *
 *
 *
 *
 *
 *
 *
 */
struct navigation_way {
	struct navigation_way *next;	/**< Pointer to a linked-list of all navigation_ways from this navigation item */
	short dir;						/**< The direction -1 or 1 of the way */
	short angle2;					/**< The angle one has to steer to drive from the old item to this street */


	/* I have been puzzled by the names of variables frequently, even up to the point that
	 * time was wasted that could have been used for better things.
	 * Those names of angles are somtimes special too. After reading short angle2 (above)
	 *
	 * I hope I am completely wrong and angle2 != angle_to
	 * but if angle2 really means angle_to then this is a bad joke
	 *
	 */



	int flags;						/**< The flags of the way */
	struct item item;				/**< The item of the way */
	char *name;						/* for street_name */
	char *name_systematic;			/* for street_name_systematic */
	char *lanes;					/* for street_lanes */
	char *destination;				/* for the destination this way leads to (osm:destination)*/
};

struct navigation_itm {
	struct navigation_way way;
	int angle_end;
	struct coord start,end;
	int time;
	int length;
	int speed;
	int dest_time;
	int dest_length;
	int told;							/**< Indicates if this item's announcement has been told earlier and should not be told again*/
	int streetname_told;				/**< Indicates if this item's streetname has been told in speech navigation*/
	int dest_count;
	struct navigation_itm *next;
	struct navigation_itm *prev;
};

static void navigation_flush(struct navigation *this_);

/**
 * @brief Calculates the delta between two angles
 * @param angle1 The first angle
 * @param angle2 The second angle
 * @return The difference between the angles: -179..-1=angle2 is left of angle1,0=same,1..179=angle2 is right of angle1,180=angle1 is opposite of angle2
 */ 


/* below anngle1 and angle2 seem more sensibly used as in 1,2,3,
 * They have no direct relation with angle2 a few lines higher,
 * but I think you can do something like angle2(from above)=angle_delta(angle1, angle2)
 *
 *
 */




static int
angle_delta(int angle1, int angle2)
{
	int delta=angle2-angle1;
	if (delta <= -180)
		delta+=360;
	if (delta > 180)
		delta-=360;
	return delta;
}

static int
angle_median(int angle1, int angle2)
{
	int delta=angle_delta(angle1, angle2);
	int ret=angle1+delta/2;
	if (ret < 0)
		ret+=360;
	if (ret > 360)
		ret-=360;
	return ret;
}

static int
angle_opposite(int angle)
{
	return ((angle+180)%360);
}

int
navigation_get_attr(struct navigation *this_, enum attr_type type, struct attr *attr, struct attr_iter *iter)
{
	struct map_rect *mr;
	struct item *item;
	dbg(0,"enter %s\n", attr_to_name(type));
	switch (type) {
	case attr_map:
		attr->u.map=this_->map;
		break;
	case attr_item_type:
	case attr_length:
	case attr_navigation_speech:
	case attr_street_name:
	case attr_street_name_systematic:
	case attr_street_name_systematic_nat:
	case attr_street_lanes:
	case attr_destination:

		/*the usage of the word 'destination' may have to be verified all-over as not
		 * to have any confusion with the destination you gave Navit as target.
		 * I may have missed out somewhere, but in OSM the word destination is there
		 * and has another meaning than in a classic Navit.
		 *
		 *
		 */

		mr=map_rect_new(this_->map, NULL);
		while ((item=map_rect_get_item(mr))) {
			if (item->type != type_nav_none && item->type != type_nav_position) {
				if (type == attr_item_type) 
					attr->u.item_type=item->type;
				else { 
					if (!item_attr_get(item, type, attr))
						item=NULL;
				}
				break;
			}
		}
		map_rect_destroy(mr);
		if (!item)
			return 0;
		break;
	case attr_turn_around_count:
		attr->u.num=this_->turn_around_count;
		break;
	default:
		return navit_object_get_attr((struct navit_object *)this_, type, attr, iter);
	}
	attr->type=type;
	return 1;
}

static void
navigation_set_turnaround(struct navigation *this_, int val)
{
	if (this_->turn_around_count != val) {
		struct attr attr=ATTR_INT(turn_around_count, val);
		this_->turn_around_count=val;
		navit_object_callbacks((struct navit_object *)this_, &attr);
	}
}

int
navigation_set_attr(struct navigation *this_, struct attr *attr)
{
	switch (attr->type) {
	case attr_speech:
		this_->speech=attr->u.speech;
		break;
	default:
		break;
	}
	return navit_object_set_attr((struct navit_object *)this_, attr);
}


struct navigation *
navigation_new(struct attr *parent, struct attr **attrs)
{
	int i,j;
	struct attr * attr;
	struct navigation *ret=(struct navigation *)navit_object_new(attrs, &navigation_func, sizeof(struct navigation));
	ret->hash=item_hash_new();
	ret->callback=callback_list_new();
	ret->callback_speech=callback_list_new();
	ret->level_last=4;
	ret->distance_turn=50;
	ret->turn_around_limit=3;
	ret->navit=parent->u.navit;
	ret->tell_street_name=1;

	for (j = 0 ; j <= route_item_last-route_item_first ; j++) {
		for (i = 0 ; i < 3 ; i++) {
			ret->announce[j][i]=-1;
		}
	}

	if ((attr=attr_search(attrs, NULL, attr_tell_street_name))) {
		ret->tell_street_name = attr->u.num;
	}
	if ((attr=attr_search(attrs, NULL, attr_delay))) {
		ret->delay = attr->u.num;
	}
	if ((attr=attr_search(attrs, NULL, attr_flags))) {
		ret->flags = attr->u.num;
	}
	return ret;	
}

int
navigation_set_announce(struct navigation *this_, enum item_type type, int *level)
{
	int i;
	if (type < route_item_first || type > route_item_last) {
		dbg(0,"street type %d out of range [%d,%d]", type, route_item_first, route_item_last);
		return 0;
	}
	for (i = 0 ; i < 3 ; i++) 
		this_->announce[type-route_item_first][i]=level[i];
	return 1;
}

static int
navigation_get_announce_level(struct navigation *this_, enum item_type type, int dist)
{
	int i;

	if (type < route_item_first || type > route_item_last)
		return -1;
	for (i = 0 ; i < 3 ; i++) {
		if (dist <= this_->announce[type-route_item_first][i])
			return i;
	}
	return i;
}




static int is_way_allowed(struct navigation *nav, struct navigation_way *way, int mode);

static int
navigation_get_announce_level_cmd(struct navigation *this_, struct navigation_itm *itm, struct navigation_command *cmd, int distance)
{
	int level2,level=navigation_get_announce_level(this_, itm->way.item.type, distance);
	if (this_->cmd_first->itm->prev) {
		level2=navigation_get_announce_level(this_, cmd->itm->prev->way.item.type, distance);
		if (level2 > level)
			level=level2;
	}
	return level;
}

/* 0=N,90=E */
static int
road_angle(struct coord *c1, struct coord *c2, int dir)
{
	int ret=transform_get_angle_delta(c1, c2, dir);
	dbg(1, "road_angle(0x%x,0x%x - 0x%x,0x%x)=%d\n", c1->x, c1->y, c2->x, c2->y, ret);
	return ret;
}

static const char
*get_count_str(int n) 
{
	switch (n) {
	case 0:
		/* TRANSLATORS: the following counts refer to streets */
		return _("zeroth"); /* Not sure if this exists, neither if it will ever be needed */
	case 1:
		return _("first");
	case 2:
		return _("second");
	case 3:
		return _("third");
	case 4:
		return _("fourth");
	case 5:
		return _("fifth");
	case 6:
		return _("sixth");
	default: 
		return NULL;
	}
}

static const char
*get_exit_count_str(int n) 
{
	switch (n) {
	case 0:
		/* TRANSLATORS: the following counts refer to roundabout exits */
		return _("zeroth exit"); /* Not sure if this exists, neither if it will ever be needed */
	case 1:
		return _("first exit");
	case 2:
		return _("second exit");
	case 3:
		return _("third exit");
	case 4:
		return _("fourth exit");
	case 5:
		return _("fifth exit");
	case 6:
		return _("sixth exit");
	default: 
		return NULL;
	}
}
static int
round_distance(int dist)
{
	if (dist < 100) {
		dist=(dist+5)/10;
		return dist*10;
	}
	if (dist < 250) {
		dist=(dist+13)/25;
		return dist*25;
	}
	if (dist < 500) {
		dist=(dist+25)/50;
		return dist*50;
	}
	if (dist < 1000) {
		dist=(dist+50)/100;
		return dist*100;
	}
	if (dist < 5000) {
		dist=(dist+50)/100;
		return dist*100;
	}
	if (dist < 100000) {
		dist=(dist+500)/1000;
		return dist*1000;
	}
	dist=(dist+5000)/10000;
	return dist*10000;
}

static int
round_for_vocabulary(int vocabulary, int dist, int factor)
{
	if (!(vocabulary & 256)) {
		if (factor != 1) 
			dist=(dist+factor/2)/factor;
	} else
		factor=1;
	if (!(vocabulary & 255)) {
		int i=0,d=0,m=0;
		while (distances[i] > 0) {
			if (!i || abs(distances[i]-dist) <= d) {
				d=abs(distances[i]-dist);
				m=i;
			}
			if (distances[i] > dist)
				break;
			i++;
		}
		dbg(0,"converted %d to %d with factor %d\n",dist,distances[m],factor);	
		dist=distances[m];
	}
	return dist*factor;
}

static int
vocabulary_last(int vocabulary)
{
	int i=0;
	if (vocabulary == 65535)
		return 1000;
	while (distances[i] > 0) 
		i++;
	return distances[i-1];
}

static char *
get_distance(struct navigation *nav, int dist, enum attr_type type, int is_length)
{
	int imperial=0,vocabulary=65535;
	struct attr attr;
	
	if (type == attr_navigation_long) {
		if (is_length)
			return g_strdup_printf(_("%d m"), dist);
		else
			return g_strdup_printf(_("in %d m"), dist);
	}
	if (navit_get_attr(nav->navit, attr_imperial, &attr, NULL))
		imperial=attr.u.num;
	if (nav->speech && speech_get_attr(nav->speech, attr_vocabulary_distances, &attr, NULL))
		vocabulary=attr.u.num;
	if (imperial) {
		if (dist*FEET_PER_METER < vocabulary_last(vocabulary)) {
			dist=round_for_vocabulary(vocabulary, dist*FEET_PER_METER, 1);
			if (is_length)
				return g_strdup_printf(_("%d feet"), dist);
			else
				return g_strdup_printf(_("in %d feet"), dist);
		}
	} else {
		if (dist < vocabulary_last(vocabulary)) {
			dist=round_for_vocabulary(vocabulary, dist, 1);
			if (is_length)
				return g_strdup_printf(_("%d meters"), dist);
			else
				return g_strdup_printf(_("in %d meters"), dist);
		}
	}
	if (imperial)
		dist=round_for_vocabulary(vocabulary, dist*FEET_PER_METER*1000/FEET_PER_MILE, 1000);
	else
		dist=round_for_vocabulary(vocabulary, dist, 1000);
	if (dist < 5000) {
		int rem=(dist/100)%10;
		if (rem) {
			if (imperial) {
				if (is_length)
					return g_strdup_printf(_("%d.%d miles"), dist/1000, rem);
				else
					return g_strdup_printf(_("in %d.%d miles"), dist/1000, rem);
			} else {
				if (is_length)
					return g_strdup_printf(_("%d.%d kilometers"), dist/1000, rem);
				else
					return g_strdup_printf(_("in %d.%d kilometers"), dist/1000, rem);
			}
		}
	}
	if (imperial) {
		if (is_length) 
			return g_strdup_printf(navit_nls_ngettext("one mile","%d miles", dist/1000), dist/1000);
		else
			return g_strdup_printf(navit_nls_ngettext("in one mile","in %d miles", dist/1000), dist/1000);
	} else {
		if (is_length) 
			return g_strdup_printf(navit_nls_ngettext("one kilometer","%d kilometers", dist/1000), dist/1000);
		else
			return g_strdup_printf(navit_nls_ngettext("in one kilometer","in %d kilometers", dist/1000), dist/1000);
	}
}


/**
 * @brief This calculates the angle with which an item (navigation_way) starts or ends
 *
 *
 * This function can be used to get the angle an item (from a route graph map)
 * starts or ends with. Note that the angle will point towards the inner of
 * the item. This function also sets the name and name_systematic of the
 * naviagtion_way item by recovering them from the real item
 *
 * this is all a little flou, why the names here ??
 *
 * This is meant to be used with items from a route graph map
 * With other items this will probably not be optimal...
 *
 * @param w The way which should be calculated and names set
 */ 
static void
calculate_entry_angle(struct navigation_way *w)
{
	struct coord cbuf[2];
	struct item *realitem;
	struct coord c;
	struct map_rect *mr;
	struct attr attr;

	w->angle2=361;
	mr = map_rect_new(w->item.map, NULL);
	if (!mr)
		return;

	realitem = map_rect_get_item_byid(mr, w->item.id_hi, w->item.id_lo);
	if (!realitem) {
		dbg(1,"Item from segment not found on map!\n");
		map_rect_destroy(mr);
		return;
	}

	if (realitem->type < type_line || realitem->type >= type_area) {
		map_rect_destroy(mr);
		return;
	}
	if (item_attr_get(realitem, attr_flags, &attr))
		w->flags=attr.u.num;
	else
		w->flags=0;
	if (item_attr_get(realitem, attr_street_name, &attr))
		w->name=map_convert_string(realitem->map,attr.u.str);
	else
		w->name=NULL;
	if (item_attr_get(realitem, attr_street_name_systematic, &attr))
		w->name_systematic=map_convert_string(realitem->map,attr.u.str);
	else
		w->name=NULL;
		
	if (w->dir < 0) {
		if (item_coord_get(realitem, cbuf, 2) != 2) {
			dbg(1,"Using calculate_angle() with a less-than-two-coords-item?\n");
			map_rect_destroy(mr);
			return;
		}
			
		while (item_coord_get(realitem, &c, 1)) {
			cbuf[0] = cbuf[1];
			cbuf[1] = c;
		}
		
	} else {
		if (item_coord_get(realitem, cbuf, 2) != 2) {
			dbg(1,"Using calculate_angle() with a less-than-two-coords-item?\n");
			map_rect_destroy(mr);
			return;
		}
		c = cbuf[0];
		cbuf[0] = cbuf[1];
		cbuf[1] = c;
	}

	map_rect_destroy(mr);

	w->angle2=road_angle(&cbuf[1],&cbuf[0],0);
}

/**
 * @brief Returns the time (in seconds) one will drive between two navigation items
 *
 * This function returns the time needed to drive between two items, including both of them,
 * in seconds.
 *
 * @param from The first item
 * @param to The last item
 * @return The travel time in seconds, or -1 on error
 */
static int
navigation_time(struct navigation_itm *from, struct navigation_itm *to)
{
	struct navigation_itm *cur;
	int time;

	time = 0;
	cur = from;
	while (cur) {
		time += cur->time;

		if (cur == to) {
			break;
		}
		cur = cur->next;
	}

	if (!cur) {
		return -1;
	}

	return time;
}

/**
 * @brief Clears the ways one can drive from itm
 *
 * @param itm The item that should have its ways cleared
 */
static void
navigation_itm_ways_clear(struct navigation_itm *itm)
{
	struct navigation_way *c,*n;

	/* At a given point I was trying out new features sometimes as fast as
	 * a new one each hour. That had to go wrong one day and indeed it broke
	 * completely soon afterwards. I then tried to restore it as good as possible
	 * so I could make a few more showcases so all efforts would not be invain.
	 * I brought it back to the point where you can drive the demovehicle as long
	 * as you want, but it freaks-out once it reaches it's destination.
	 * Apparently this also happens when you try to acces the route, I never tried
	 * that and would even have been surprised if that already wroked.
	 *
	 *
	 *
	 * Below is a good sample of all the clumsyness I ended up doing, all for the good cause,
	 * but would appreciate stuff like that to be reviewed before high_five makes it to any
	 * kind of public repo. This is something that must first be seen in action, without
	 * browsing throught the code first. So in short, a first public available version should be
	 * able to drive to destination without errors, route access and navigation map can be dealt
	 * with along the way.
	 *
	 * IMHO the usage of map_convert_free is wrong, but since it works fine this can be dealt with later
	 *
	 */





	c = itm->way.next;
	while (c) {
		n = c->next;
		dbg(0,"717 itm_ways_clear\n");
		map_convert_free(c->name);
//		map_convert_free(c->name_systematic);
//		map_convert_free(c->destination);
//		map_convert_free(c->lanes);

		g_free(c);
		c = n;
	}

	itm->way.next = NULL;
}

/**
 * @brief Updates the ways one can drive from itm
 *
 * This updates the list of possible ways to drive to from itm. The item "itm" is on
 * and the next navigation item are excluded.
 *
 * @param itm The item that should be updated
 * @param graph_map The route graph's map that these items are on 
 */
static void
navigation_itm_ways_update(struct navigation_itm *itm, struct map *graph_map) 
{
	struct map_selection coord_sel;
	struct map_rect *g_rect; /* Contains a map rectangle from the route graph's map */
	struct item *i,*sitem;
	struct attr sitem_attr,direction_attr;
	struct navigation_way *w,*l;

	navigation_itm_ways_clear(itm);

	/* These values cause the code in route.c to get us only the route graph point and connected segments */
	coord_sel.next = NULL;
	coord_sel.u.c_rect.lu = itm->start;
	coord_sel.u.c_rect.rl = itm->start;
	/* the selection's order is ignored */
	
	g_rect = map_rect_new(graph_map, &coord_sel);
	
	i = map_rect_get_item(g_rect);
	if (!i || i->type != type_rg_point) { /* probably offroad? */
		map_rect_destroy(g_rect);
		return ;
	}

	w = NULL;
	
	while (1) {
		i = map_rect_get_item(g_rect);

		if (!i) {
			break;
		}
		
		if (i->type != type_rg_segment) {
			continue;
		}
		
		if (!item_attr_get(i,attr_street_item,&sitem_attr)) {
			dbg(1, "Got no street item for route graph item in entering_straight()\n");
			continue;
		}		

		if (!item_attr_get(i,attr_direction,&direction_attr)) {
			continue;
		}

		sitem = sitem_attr.u.item;
		if (sitem->type == type_street_turn_restriction_no || sitem->type == type_street_turn_restriction_only)
			continue;

		if (item_is_equal(itm->way.item,*sitem) || ((itm->prev) && item_is_equal(itm->prev->way.item,*sitem))) {
			continue;
		}

		l = w;
		w = g_new(struct navigation_way, 1);
		w->dir = direction_attr.u.num;
		w->item = *sitem;
		w->next = l;
		calculate_entry_angle(w);	/* calculte and set w->angle2 */
	}

	map_rect_destroy(g_rect);
	
	itm->way.next = w;
}

static void
navigation_destroy_itms_cmds(struct navigation *this_, struct navigation_itm *end)
{
	struct navigation_itm *itm;
	struct navigation_command *cmd;
	dbg(2,"enter this_=%p this_->first=%p this_->cmd_first=%p end=%p\n", this_, this_->first, this_->cmd_first, end);
	if (this_->cmd_first)
		dbg(2,"this_->cmd_first->itm=%p\n", this_->cmd_first->itm);
	while (this_->first && this_->first != end) {
		itm=this_->first;
		dbg(3,"destroying %p\n", itm);
		item_hash_remove(this_->hash, &itm->way.item);
		this_->first=itm->next;
		if (this_->first)
			this_->first->prev=NULL;
		if (this_->cmd_first && this_->cmd_first->itm == itm->next) {
			cmd=this_->cmd_first;
			this_->cmd_first=cmd->next;
			if (cmd->next) {
				cmd->next->prev = NULL;
			}
			g_free(cmd);
		}
		dbg(0,"829 destroy_itm_cmds\n");
		map_convert_free(itm->way.name);
		map_convert_free(itm->way.name_systematic);
		navigation_itm_ways_clear(itm);
		g_free(itm);
	}
	if (! this_->first)
		this_->last=NULL;
	if (! this_->first && end) 
		dbg(0,"end wrong\n");
	dbg(2,"ret this_->first=%p this_->cmd_first=%p\n",this_->first, this_->cmd_first);
}

static void
navigation_itm_update(struct navigation_itm *itm, struct item *ritem)
{
	struct attr length, time, speed;

	if (! item_attr_get(ritem, attr_length, &length)) {
		dbg(0,"no length\n");
		return;
	}
	if (! item_attr_get(ritem, attr_time, &time)) {
		dbg(0,"no time\n");
		return;
	}
	if (! item_attr_get(ritem, attr_speed, &speed)) {
		dbg(0,"no time\n");
		return;
	}

	dbg(1,"length=%ld time=%ld speed=%ld\n", length.u.num, time.u.num, speed.u.num);
	itm->length=length.u.num;
	itm->time=time.u.num;
	itm->speed=speed.u.num;
}

/**
 * @brief This check if an item is part of a roundabout
 *
 * @param itm The item to be checked
 * @return True if the item is part of a roundabout
 */ 
static int
check_roundabout(struct navigation_itm *itm, struct map *graph_map)
{
	struct map_selection coord_sel;
	struct map_rect *g_rect; /* Contains a map rectangle from the route graph's map */
	struct item *i,*sitem;
	struct attr sitem_attr,flags_attr;

	/* These values cause the code in route.c to get us only the route graph point and connected segments */
	coord_sel.next = NULL;
	coord_sel.u.c_rect.lu = itm->start;
	coord_sel.u.c_rect.rl = itm->start;
	/* the selection's order is ignored */
	
	g_rect = map_rect_new(graph_map, &coord_sel);
	
	i = map_rect_get_item(g_rect);
	if (!i || i->type != type_rg_point) { /* probably offroad? */
		map_rect_destroy(g_rect);
		return 0;
	}

	while (1) {
		i = map_rect_get_item(g_rect);

		if (!i) {
			break;
		}
		
		if (i->type != type_rg_segment) {
			continue;
		}
		
		if (!item_attr_get(i,attr_street_item,&sitem_attr)) {
			continue;
		}		

		sitem = sitem_attr.u.item;
		if (item_is_equal(itm->way.item,*sitem)) {
			if (item_attr_get(i,attr_flags,&flags_attr) && (flags_attr.u.num & AF_ROUNDABOUT)) {
				map_rect_destroy(g_rect);
				return 1;
			}
		}
	}

	map_rect_destroy(g_rect);
	return 0;
}

/*@brief
 *
 *
 * hier die exit ref recupereren en als naam zetten !!
 *
 *
 *
 * routeitem has an attr. streetitem, but that is only and id and a map,
 * allowing to fetch the actual streetitem, that will live under the same name.
 * I suggest the following change, the streetitem we fetch from the map gets a distinct
 * name, the original streetitem is preserved untill we have no need for it's map anymore,
 * instead of preserving a link to the map of the original streetitem (tmap)
 * Solves a confusion and improves my first-try solution
 * (medium priority, has no functional effect)
 *
 *
 * navigation_itm_new() holds the bulk of the changes of high_five
 * and have a look at the maptool diff
 * (#1082, my recent version, robotaxi's version is outdated) before reading below.
 *
 *
 */


static struct navigation_itm *
navigation_itm_new(struct navigation *this_, struct item *routeitem)
{
	struct navigation_itm *ret=g_new0(struct navigation_itm, 1);
	int i=0;
	struct item *streetitem;
	struct map *graph_map = NULL;
	struct attr street_item,direction,route_attr;
	struct map_rect *mr;
	struct attr attr;
	struct coord c[256];		/* however the OSM maximum is 2000 nodes */
	struct coord exitcoord;

	if (routeitem) {
		ret->streetname_told=0;
		if (! item_attr_get(routeitem, attr_street_item, &street_item)) {
			g_free(ret);
			ret = NULL;
			return ret;
		}

		if (item_attr_get(routeitem, attr_direction, &direction))
			ret->way.dir=direction.u.num;
		else
			ret->way.dir=0;

		streetitem=street_item.u.item;
		ret->way.item=*streetitem;
		item_hash_insert(this_->hash, streetitem, ret);

		mr=map_rect_new(streetitem->map, NULL);  /* huge map but ok for get_item_byid */

		struct map *tmap = streetitem->map;  /*find better name for backup pointer to map*/

		if (! (streetitem=map_rect_get_item_byid(mr, streetitem->id_hi, streetitem->id_lo))) {
			g_free(ret);
			map_rect_destroy(mr);
			return NULL;
		}

		if (item_attr_get(streetitem, attr_street_name, &attr)){

			ret->way.name=map_convert_string(streetitem->map,attr.u.str);

		}

		/* for highways OSM ref, nat_ref and int_ref can get a bit fuzzy.
		 *
		 * Most of the times it looks like if we have nat_ref then ref actually
		 * holds int_ref, and if we have int_ref then ref actually holds nat_ref.
		 * Did not come across one that had all three in use.
		 *
		 * todo : combine these in a uniform way into way.name_systematic,
		 * so we have something like (int_ref/nat_ref) in a consistant manner.
		 * now there is some randomness, caused by the OSM data
		 *
		 * (medium or lower priority)
		 *
		 */


		if (item_attr_get(streetitem, attr_street_name_systematic, &attr)){
			ret->way.name_systematic=map_convert_string(streetitem->map,attr.u.str);

		}

		/* handle later, see todo above.
		*if (item_attr_get(streetitem, attr_street_name_systematic_nat, &attr)){
		*			ret->way.name2_nat=map_convert_string(streetitem->map,attr.u.str);
		*
		*	}
		*/

		if (item_attr_get(streetitem, attr_street_destination, &attr)){
			ret->way.destination=map_convert_string(streetitem->map,attr.u.str);
	/*				dbg(0,"destination=%s\n", ret->way.destination); */
		}


		navigation_itm_update(ret, routeitem);


		/* there must be a faster way to obtain only first and last coord
		 * ask tryagain someday ?
		 *
		 *
		 * low priority, works fine as is for a long time already
		 */

		item_coord_rewind(routeitem);
		while (i<255 && item_coord_get(routeitem, &c[i], 1) ) {
/*			dbg(0, "coord %i %i %i\n", i, c[i].x ,c[i].y);   */
				i++;
		}
/*		dbg(0,"count=%d\n", i); */
		i--;
		if (i>=1)
		{
			ret->way.angle2=road_angle(&c[0], &c[1], 0);
			ret->angle_end=road_angle(&c[i-1], &c[i], 0);
		}
		ret->start=c[0];
		ret->end=c[i];

		/*	If we have a ramp check the map for higway_exit info,
		 *  but only on the first node of the ramp.
		 *  Ramps with nodes in reverse order and oneway=-1 are not
		 *  specifically handled, but no occurence known so far either.
		 *  If present, obtain exit_ref, exit_label and exit_to
		 *  from the map.
		 *  exit_to holds info similar to attr_street_destination, and
		 *  we place it in way.destination as well, replacing the street_destination info
		 *  in cases where a ramp has both.
		 *
		 *
		 */
		if (streetitem->type == type_ramp )
		{


			struct map_selection mselexit;
			struct item *rampitem;
			dbg(0,"test ramp\n");

			mselexit.next = NULL;
			mselexit.u.c_rect.lu = c[0] ;
			mselexit.u.c_rect.rl = c[0] ;
			/* it's probably futile to start changing the params below
			 * and might even turn against us some day so pls. leave as is
			 */
			mselexit.range = item_range_all;
			mselexit.order = 18;

			map_rect_destroy(mr);					/* is this usefull ? */
			mr = map_rect_new	(tmap, &mselexit);

			while (rampitem=map_rect_get_item(mr)) /* generates a compiler warning but Navit does
													* it like this all the time
													*/
			{

				if (rampitem->type == type_highway_exit

						/*  Don't do anything like below, as I see it now we also pick up
						 *  some info from time to time in case a ramp splits.
						 *  If you do anything like below you will start cutting away info.
						 *
						 *		&& ((this_->last->way.item.type == type_highway_land) || (this_->last->way.item.type == type_highway_city))
						 * 		or check with is_motorway_like() or don't check at all ?
						 */
								)
				{
					if (item_coord_get(rampitem, &exitcoord, 1)
							&& exitcoord.x == c[0].x && exitcoord.y == c[0].y)
					{


						/* dbg 0 is for errors only, I mis-use it in a first-try
						 * version too, but in public versions this should be cleaned-up all over.
						 *
						 * and why not make it mandatory that dbg 0 messages start with 'ERROR:'
						 * or make dbg() do that ?
						 *
						 */

	/*					dbg(0,"coords :%i, %i \n",exitcoord.x,exitcoord.y);	*/

						while (item_attr_get(rampitem, attr_any, &attr))
						{
							if (attr.type)
							{
								if (attr.type == attr_label)
								{
									dbg(0,"exit_label=%s\n",attr.u.str);
									ret->way.name_systematic= map_convert_string(streetitem->map,attr.u.str);
								}
								if (attr.type == attr_ref)
								{
									dbg(0,"exit_ref=%s\n",attr.u.str);
									ret->way.name= map_convert_string(streetitem->map,attr.u.str);
								}

								if (attr.type == attr_exit_to)
								{
									/* some exit_to info was found to be csv instead of
									 * using a ; sep.
									 *
									 * the code from robotaxi will have to be reviewed for this,
									 * it only handles ; separator now.
									 *
									 *
									 * If OSM contains no anomalies, it's either exit_to or
									 * destnation info, but we must be aware that both could
									 * be provided for the same street.
									 * If both exist, the info from exit_to
									 * will survive, will need a review later.
									 * priotiry : low, works already really fine this way and
									 * it will require much testing or map-data scanning to find out
									 * if this can be a real issue or only my imagination.
									 *
									 *
									 */
									if (attr.u.str){
										if (ret->way.destination)
										map_convert_free(ret->way.destination);
					/*				dbg(0,"exit_to=%s\n",attr.u.str); */
									ret->way.destination= map_convert_string(streetitem->map,attr.u.str);
									}
								}


							}
						}
					}
				}
			}
		}

		item_attr_get(routeitem, attr_route, &route_attr);
		graph_map = route_get_graph_map(route_attr.u.route);
		if (check_roundabout(ret, graph_map)) {
			ret->way.flags |= AF_ROUNDABOUT;
		}

/*		dbg(1,"i=%d start %d end %d '%s' '%s'\n", i, ret->way.angle2, ret->angle_end, ret->way.name, ret->way.name_systematic); */
		map_rect_destroy(mr);
	} else {
		if (this_->last)
			ret->start=ret->end=this_->last->end;
	}
	if (! this_->first)
		this_->first=ret;
	if (this_->last) {
		this_->last->next=ret;
		ret->prev=this_->last;
		if (graph_map) {
			navigation_itm_ways_update(ret,graph_map);
		}
	}
	dbg(1,"ret=%p\n", ret);
	this_->last=ret;
	return ret;
}

/**
 * @brief Counts how many times a driver could turn right/left 
 *
 * This function counts how many times the driver theoretically could
 * turn right/left between two navigation items, not counting the final
 * turn itself.
 *
 * @param from The navigation item which should form the start
 * @param to The navigation item which should form the end
 * @param direction Set to < 0 to count turns to the left >= 0 for turns to the right
 * @return The number of possibilities to turn or -1 on error
 */
static int
count_possible_turns(struct navigation *nav, struct navigation_itm *from, struct navigation_itm *to, int direction)
{
	int count;
	struct navigation_itm *curr;
	struct navigation_way *w;

	count = 0;
	curr = from->next;
	while (curr && (curr != to)) {
		w = curr->way.next;

		while (w) {
			if (is_way_allowed(nav, w, 4)) {
				if (direction < 0) {
					if (angle_delta(curr->prev->angle_end, w->angle2) < 0) {
						count++;
						break;
					}
				} else {
					if (angle_delta(curr->prev->angle_end, w->angle2) > 0) {
						count++;
						break;
					}				
				}
			}
			w = w->next;
		}
		curr = curr->next;
	}

	if (!curr) { /* from does not lead to to? */
		return -1;
	}

	return count;
}

/**
 * @brief Calculates distance and time to the destination
 *
 * This function calculates the distance and the time to the destination of a
 * navigation. If incr is set, this is only calculated for the first navigation
 * item, which is a lot faster than re-calculation the whole destination, but works
 * only if the rest of the navigation already has been calculated.
 *
 * @param this_ The navigation whose destination / time should be calculated
 * @param incr Set this to true to only calculate the first item. See description.
 */
static void
calculate_dest_distance(struct navigation *this_, int incr)
{
	int len=0, time=0, count=0;
	struct navigation_itm *next,*itm=this_->last;
	dbg(1, "enter this_=%p, incr=%d\n", this_, incr);
	if (incr) {
		if (itm) {
			dbg(2, "old values: (%p) time=%d lenght=%d\n", itm, itm->dest_length, itm->dest_time);
		} else {
			dbg(2, "old values: itm is null\n");
		}
		itm=this_->first;
		next=itm->next;
		dbg(2, "itm values: time=%d lenght=%d\n", itm->length, itm->time);
		dbg(2, "next values: (%p) time=%d lenght=%d\n", next, next->dest_length, next->dest_time);
		itm->dest_length=next->dest_length+itm->length;
		itm->dest_count=next->dest_count+1;
		itm->dest_time=next->dest_time+itm->time;
		dbg(2, "new values: time=%d lenght=%d\n", itm->dest_length, itm->dest_time);
		return;
	}
	while (itm) {
		len+=itm->length;
		time+=itm->time;
		itm->dest_length=len;
		itm->dest_time=time;
		itm->dest_count=count++;
		itm=itm->prev;
	}
	dbg(1,"len %d time %d\n", len, time);
}

/**
 * @brief Checks if two navigation items are on the same street
 *
 * This function checks if two navigation items are on the same street. It returns
 * true if either their name or their "systematic name" (e.g. "A6" or "B256") are the
 * same.
 *
 * @param old The first item to be checked
 * @param new The second item to be checked
 * @return True if both old and new are on the same street
 */
static int
is_same_street2(char *old_name, char *old_name_systematic, char *new_name, char *new_name_systematic)
{
	if (old_name && new_name && !strcmp(old_name, new_name)) {
/*		dbg(1,"is_same_street: '%s' '%s' vs '%s' '%s' yes (1.)\n", old_name2, new_name2, old_name1, new_name1); */
		return 1;
	}
	if (old_name_systematic && new_name_systematic && !strcmp(old_name_systematic, new_name_systematic)) {
/*		dbg(1,"is_same_street: '%s' '%s' vs '%s' '%s' yes (2.)\n", old_name2, new_name2, old_name1, new_name1); */
		return 1;
	}
/*	dbg(1,"is_same_street: '%s' '%s' vs '%s' '%s' no\n", old_name, new_name, old_name, new_name);*/
	return 0;
}


/* Don't think we want conditionals in it, and I think in general it is
 * ok to do so to handle platform specific issues and very good to enable building a debugging version,
 * the rest only for a brief period
 * of time, then decide to remove the conditional and make it a permanent part or
 * ditch it.
 */






#if 0
/**
 * @brief Checks if two navigation items are on the same street
 *
 * This function checks if two navigation items are on the same street. It returns
 * true if the first part of their "systematic name" is equal. If the "systematic name" is
 * for example "A352/E3" (a german highway which at the same time is part of the international
 * E-road network), it would only search for "A352" in the second item's systematic name.
 *
 * @param old The first item to be checked
 * @param new The second item to be checked
 * @return True if the "systematic name" of both items matches. See description.
 */
static int
is_same_street_systematic(struct navigation_itm *old, struct navigation_itm *new)
{
	int slashold,slashnew;
	if (!old->name2 || !new->name2)
		return 1;
	slashold=strcspn(old->name2, "/");
	slashnew=strcspn(new->name2, "/");
	if (slashold != slashnew || strncmp(old->name2, new->name2, slashold))
		return 0;
	return 1;
}


/**
 * @brief Check if there are multiple possibilities to drive from old
 *
 * This function checks, if there are multiple streets connected to the exit of "old".
 * Sometimes it happens that an item on a map is just segmented, without any other streets
 * being connected there, and it is not useful if navit creates a maneuver there.
 *
 * @param new The navigation item we're driving to
 * @return True if there are multiple streets
 */
static int 
maneuver_multiple_streets(struct navigation_itm *new)
{
	if (new->way.next) {
		return 1;
	} else {
		return 0;
	}
}


/**
 * @brief Check if the new item is entered "straight"
 *
 * This function checks if the new item is entered "straight" from the old item, i.e. if there
 * is no other street one could take from the old item on with less steering.
 *
 * @param new The navigation item we're driving to
 * @param diff The absolute angle one needs to steer to drive to this item
 * @return True if the new item is entered "straight"
 */
static int 
maneuver_straight(struct navigation_itm *new, int diff)
{
	int curr_diff;
	struct navigation_way *w;

	w = new->way.next;
	dbg(1,"diff=%d\n", diff);
	while (w) {
		curr_diff=abs(angle_delta(new->prev->angle_end, w->angle2));
		dbg(1,"curr_diff=%d\n", curr_diff);
		if (curr_diff < diff) {
			return 0;
		}
		w = w->next;
	}
	return 1;
}
#endif

static int maneuver_category(enum item_type type)
{
	switch (type) {
	case type_street_0:
		return 1;
	case type_street_1_city:
		return 2;
	case type_street_2_city:
		return 3;
	case type_street_3_city:
		return 4;
	case type_street_4_city:
		return 5;
	case type_highway_city:
		return 7;
	case type_street_1_land:
		return 2;
	case type_street_2_land:
		return 3;
	case type_street_3_land:
		return 4;
	case type_street_4_land:
		return 5;
	case type_street_n_lanes:
		return 6;
	case type_highway_land:
		return 7;
	case type_ramp:
		return 0;
	case type_roundabout:
		return 0;
	case type_ferry:
		return 0;
	default:
		return 0;
	}
	
	
}

static int
is_way_allowed(struct navigation *nav, struct navigation_way *way, int mode)
{
	if (!nav->vehicleprofile)
		return 1;
	return !way->flags || ((way->flags & (way->dir >= 0 ? nav->vehicleprofile->flags_forward_mask : nav->vehicleprofile->flags_reverse_mask)) == nav->vehicleprofile->flags);
}

/**
 * @brief Checks whether a way has motorway-like characteristics
 *
 * Motorway-like means one of the following:
 *
 * item type is highway_land or highway_city (OSM: highway=motorway)
 * item type is street_n_lanes (OSM: highway=trunk) and way is one-way
 *
 * @param way The way to examine
 * @return True for motorway-like, false otherwise
 */
static int
is_motorway_like(struct navigation_way *way)
{
	if ((way->item.type == type_highway_land) || (way->item.type == type_highway_city)  || ((way->item.type == type_street_n_lanes) && (way->flags & AF_ONEWAYMASK)))
		return 1;
	return 0;
}

/**
 * @brief Checks if navit has to create a maneuver to drive from old to new
 *
 * This function checks if it has to create a "maneuver" - i.e. guide the user - to drive 
 * from "old" to "new".
 *
 * @param old The old navigation item, where we're coming from
 * @param new The new navigation item, where we're going to
 * @param delta The angle the user has to steer to navigate from old to new
 * @param reason A text string explaining how the return value resulted
 * @return True if navit should guide the user, false otherwise
 */
static int
maneuver_required2 (struct navigation *nav, struct navigation_itm *old, struct navigation_itm *new, int *delta, char **reason)
{
	int ret=0,d,dw,dlim;
	char *r=NULL;
	struct navigation_way *w;
	int cat,ncat,wcat,maxcat,left=-180,right=180,is_unambigous=0,is_same_street;
	int curve_limit=25;

/*	dbg(1,"enter %p %p %p\n",old, new, delta); */
	d=angle_delta(old->angle_end, new->way.angle2);
/*	dbg(0,"old=%s %s, new=%s %s, angle old=%d, angle new=%d, d=%i\n ",old->way.name,old->way.name_systematic,new->way.name,new->way.name_systematic,old->angle_end, new->way.angle2,d); */
	if (!new->way.next) {
		/* No announcement necessary */
		r="no: Only one possibility";
	} else if (!new->way.next->next && new->way.next->item.type == type_ramp && !is_way_allowed(nav,new->way.next,1)) {
		/* If the other way is only a ramp and it is one-way in the wrong direction, no announcement necessary */
		r="no: Only ramp and unallowed direction ";
		ret=0;
	}
	if (! r) {
		/* Announce exit from roundabout, but not entry or staying in it */
		if ((old->way.flags & AF_ROUNDABOUT) && ! (new->way.flags & AF_ROUNDABOUT)) {
			r="yes: leaving roundabout";
			ret=1;
		} else 	if (!new->way.next->next && !(old->way.flags & AF_ROUNDABOUT) && (new->way.flags & AF_ROUNDABOUT) && (new->way.next->flags & AF_ROUNDABOUT)) {
			/* this rather complicated construct makes sure we suppress announcements
			 * only when we're entering a roundabout AND there are no other options */
			r="no: entering roundabout";
		} else if ((old->way.flags & AF_ROUNDABOUT) && (new->way.flags & AF_ROUNDABOUT)) {
			r="no: staying in roundabout";
		}
	}
	if (!r) {
		if (new->way.item.type == type_ramp) {
			/* If new is a ramp, ANNOUNCE */
			r="yes: entering ramp";
			ret=1;
		} else if (is_motorway_like(&(old->way))) {
			/* If we are at a motorway interchange, ANNOUNCE
			 * We are assuming a motorway interchange when old way and at least
			 * two possible ways are motorway-like and allowed.
			 * If any of the possible ways is neither motorway-like nor a ramp,
			 * we are probably on a trunk road with level crossings and not
			 * at a motorway interchange.
			 */
			/* FIXME: motorway junctions could have service roads */
			int num_new_motorways = 0;
			int num_other = 0;
			struct navigation_way *cur_itm = &(new->way);
			while (cur_itm) {
				if ((is_motorway_like(cur_itm)) && is_way_allowed(nav,cur_itm,1)) {
					num_new_motorways++;
				} else if (cur_itm->item.type != type_ramp) {
					num_other++;
				}
				cur_itm = cur_itm->next;
			}
			if ((num_other == 0) && (num_new_motorways > 1)) {
				r="yes: motorway interchange";
				ret=1;
			}
		}
	}
	cat=maneuver_category(old->way.item.type);
	if (!r && abs(d) > 75) {
		/* always make an announcement if you have to make a sharp turn */
		r="yes: delta over 75";
		ret=1;
	} else if (!r && abs(d) > 22) {
		/* When coming from street_2_* or higher category road, check if
		 * - we have multiple options of the same category and
		 * - we have to make a considerable turn (more than 22 degrees)
		 * If both is the case, ANNOUNCE.
		 * Note: 22.5 degrees is the threshold because anything higher is
		 * closer to 45 than to 0 degrees.
		 */
		if (cat >= maneuver_category(type_street_2_city)) {
			int num_similar = 0;
			struct navigation_way *cur_itm = &(new->way);
			while (cur_itm) {
				if (maneuver_category(cur_itm->item.type) == cat) {
					/* TODO: decide if a maneuver_category difference of 1 is still similar */
					num_similar++;
				}
				cur_itm = cur_itm->next;
			}
			if (num_similar > 1) {
				ret=1;
				r="yes: more than one similar road and delta over 22";
			}
		}
	}
	ncat=maneuver_category(new->way.item.type);
	if (!r) {
		int dc=d;
		/* Check whether the street keeps its name */
		is_same_street=is_same_street2(old->way.name, old->way.name_systematic, new->way.name, new->way.name_systematic);
		w = new->way.next;
		maxcat=-1;
		while (w) {
			dw=angle_delta(old->angle_end, w->angle2);
			if (dw < 0) {
				if (dw > left)
					left=dw;
				if (dw > -curve_limit && d < 0 && d > -curve_limit)
					dc=dw;
			} else {
				if (dw < right)
					right=dw;
				if (dw < curve_limit && d > 0 && d < curve_limit)
					dc=dw;
			}
			wcat=maneuver_category(w->item.type);
			/* If any other street has the same name, we can't use the same name criterion.
			 * Exceptions apply if we're coming from a motorway-like road and:
			 * - the other road is motorway-like (a motorway might split up temporarily) or
			 * - the other road is a ramp (they are sometimes tagged with the name of the motorway)
			 * The second one is really a workaround for bad tagging practice in OSM. Since entering
			 * a ramp always creates a maneuver, we don't expect the workaround to have any unwanted
			 * side effects.
			 */

			/* ran into some trouble here below */
			if (is_same_street && is_same_street2(old->way.name,
					//old->way.name_systematic,
					w->name
					, NULL, NULL
					//, w->name_systematic
					) && (!is_motorway_like(&(old->way)) || (!is_motorway_like(w) && w->item.type != type_ramp)) && is_way_allowed(nav,w,2))
				is_same_street=0;
			/* Mark if the street has a higher or the same category */
			if (wcat > maxcat)
				maxcat=wcat;
			w = w->next;
		}
		/* get the delta limit for checking for other streets. It is lower if the street has no other
		   streets of the same or higher category */
		if (ncat < cat)
			dlim=80;
		else
			dlim=120;
		/* if the street is really straight, the others might be closer to straight */
		if (abs(d) < 20)
			dlim/=2;
		if ((maxcat == ncat && maxcat == cat) || (ncat == 0 && cat == 0)) 
			dlim=abs(d)*620/256;
		else if (maxcat < ncat && maxcat < cat)
			dlim=abs(d)*128/256;
		if (left < -dlim && right > dlim) 
			is_unambigous=1;
		if (dc != d) {
			dbg(1,"d %d vs dc %d\n",d,dc);
			d-=(dc+d+1)/2;
			dbg(1,"result %d\n",d);
			is_unambigous=0;
		}
		if (!is_same_street && is_unambigous < 1) {
			ret=1;
			r="yes: not same street or ambigous";
		} else
			r="no: same street and unambigous";


#ifdef DEBUG
		r=g_strdup_printf("yes: d %d left %d right %d dlim=%d cat old:%d new:%d max:%d unambigous=%d same_street=%d", d, left, right, dlim, cat, ncat, maxcat, is_unambigous, is_same_street);
#endif
	}

	/* the 2 below could use is_motorway_like() but in this stage apply a stricter criterion for
	 * clear testcases to work on.
	 *
	 * EDIT : by now I did so much testing that I indeed would switch to motorway_like()
	 * just did not get to it yet.
	 *
	 *
	 * tries to preserve the info obtained here by assigning r the merge or exit string for
	 * reuse when actually making or speaking the command. Needs a more refined way of passing info in
	 * the future.
	 */
	if (old->way.item.type == type_ramp && (new->way.item.type == type_highway_city || new->way.item.type == type_highway_land)) {
		ret=1;
		r="merge";
	}
	if (new->way.item.type == type_ramp && (old->way.item.type == type_highway_city || old->way.item.type == type_highway_land)) {
			ret=1;
			r="exit";
		}

	*delta=d;
	if (reason)
		*reason=r;
	dbg(0,"reason %s, delta=%i\n",r,*delta);
	return ret;
	

#if 0
	if (new->item.type == old->item.type || (new->item.type != type_ramp && old->item.type != type_ramp)) {
		if (is_same_street2(old, new)) {
			if (! entering_straight(new, abs(*delta))) {
				dbg(1, "maneuver_required: Not driving straight: yes\n");
				if (reason)
					*reason="yes: Not driving straight";
				return 1;
			}

			if (check_multiple_streets(new)) {
				if (entering_straight(new,abs(*delta)*2)) {
					if (reason)
						*reason="no: delta < ext_limit for same name";
					return 0;
				}
				if (reason)	
					*reason="yes: delta > ext_limit for same name";
				return 1;
			} else {
				dbg(1, "maneuver_required: Staying on the same street: no\n");
				if (reason)
					*reason="no: Staying on same street";
				return 0;
			}
		}
	} else
		dbg(1, "maneuver_required: old or new is ramp\n");
#if 0
	if (old->item.type == type_ramp && (new->item.type == type_highway_city || new->item.type == type_highway_land)) {
		dbg(1, "no_maneuver_required: old is ramp new is highway\n");
		if (reason)
			*reason="no: old is ramp new is highway";
		return 0;
	}
#endif
#if 0
	if (old->crossings_end == 2) {
		dbg(1, "maneuver_required: only 2 connections: no\n");
		return 0;
	}
#endif
	dbg(1,"delta=%d-%d=%d\n", new->way.angle2, old->angle_end, *delta);
	if ((new->item.type == type_highway_land || new->item.type == type_highway_city || old->item.type == type_highway_land || old->item.type == type_highway_city) && (!is_same_street_systematic(old, new) || (old->name2 != NULL && new->name2 == NULL))) {
		dbg(1, "maneuver_required: highway changed name\n");
		if (reason)
			*reason="yes: highway changed name";
		return 1;
	}
	if (abs(*delta) < straight_limit) {
		if (! entering_straight(new,abs(*delta))) {
			if (reason)
				*reason="yes: not straight";
			dbg(1, "maneuver_required: not driving straight: yes\n");
			return 1;
		}

		dbg(1, "maneuver_required: delta(%d) < %d: no\n", *delta, straight_limit);
		if (reason)
			*reason="no: delta < limit";
		return 0;
	}
	if (abs(*delta) < ext_straight_limit) {
		if (entering_straight(new,abs(*delta)*2)) {
			if (reason)
				*reason="no: delta < ext_limit";
			return 0;
		}
	}

	if (! check_multiple_streets(new)) {
		dbg(1, "maneuver_required: only one possibility: no\n");
		if (reason)
			*reason="no: only one possibility";
		return 0;
	}

	dbg(1, "maneuver_required: delta=%d: yes\n", *delta);
	if (reason)
		*reason="yes: delta >= limit";
	return 1;
#endif
}

static struct navigation_command *
command_new(struct navigation *this_, struct navigation_itm *itm, int delta, char * reason)
{
	struct navigation_command *ret=g_new0(struct navigation_command, 1);
	dbg(1,"enter this_=%p itm=%p delta=%d\n", this_, itm, delta);
	ret->reason = reason;
	ret->delta=delta;
	ret->itm=itm;
	if (itm && itm->prev && itm->way.next && itm->prev->way.next && !(itm->way.flags & AF_ROUNDABOUT) && (itm->prev->way.flags & AF_ROUNDABOUT)) {
		int len=0;
		int angle=0;
		int entry_angle;
		struct navigation_itm *itm2=itm->prev;
		int exit_angle=angle_median(itm->prev->angle_end, itm->way.next->angle2);
		dbg(1,"exit %d median from %d,%d\n", exit_angle,itm->prev->angle_end, itm->way.next->angle2);
		while (itm2 && (itm2->way.flags & AF_ROUNDABOUT)) {
			len+=itm2->length;
			angle=itm2->angle_end;
			itm2=itm2->prev;
		}
		if (itm2 && itm2->next && itm2->next->way.next) {
			itm2=itm2->next;
			entry_angle=angle_median(angle_opposite(itm2->way.angle2), itm2->way.next->angle2);
			dbg(1,"entry %d median from %d(%d),%d\n", entry_angle,angle_opposite(itm2->way.angle2), itm2->way.angle2, itm2->way.next->angle2);
		} else {
			entry_angle=angle_opposite(angle);
		}
		dbg(0,"entry %d exit %d\n", entry_angle, exit_angle);
		ret->roundabout_delta=angle_delta(entry_angle, exit_angle);
		ret->length=len+roundabout_extra_length;
	}
	if (this_->cmd_last) {
		this_->cmd_last->next=ret;
		ret->prev = this_->cmd_last;
	}
	this_->cmd_last=ret;

	if (!this_->cmd_first)
		this_->cmd_first=ret;
	return ret;
}

static void
make_maneuvers(struct navigation *this_, struct route *route)
{
	struct navigation_itm *itm, *last=NULL, *last_itm=NULL;
	int delta;
	itm=this_->first;
	this_->cmd_last=NULL;
	this_->cmd_first=NULL;
	while (itm) {
		if (last) {
			char * reason;
			if (maneuver_required2(this_, last_itm, itm,&delta,&reason)) {
				command_new(this_, itm, delta,reason);
			}
		} else
			last=itm;
		last_itm=itm;
		itm=itm->next;
	}
	command_new(this_, last_itm, 0, NULL);
}

static int
contains_suffix(char *name, char *suffix)
{
	if (!suffix)
		return 0;
	if (strlen(name) < strlen(suffix))
		return 0;
	return !navit_utf8_strcasecmp(name+strlen(name)-strlen(suffix), suffix);
}



static char *
replace_suffix(char *name, char *search, char *replace)
{
	int len=strlen(name)-strlen(search);
	char *ret=g_malloc(len+strlen(replace)+1);
	strncpy(ret, name, len);
	strcpy(ret+len, replace);
	if (isupper(name[len])) {
		ret[len]=toupper(ret[len]);
	}

	return ret;
}

/* I pretty much neglected the speach side of it and focussed
 * entirely on OSD in the first stage
 *
 * robotaxi's code already has something for the speaking of the destination
 *
 */



static char *
navigation_item_destination(struct navigation *nav, struct navigation_itm *itm, struct navigation_itm *next, char *prefix)
{
	char *ret=NULL,*name1,*sep,*name2;
	char *name,*name_systematic;
	int i,sex;
	int vocabulary1=65535;
	int vocabulary2=65535;
	struct attr attr;

	if (! prefix)
		prefix="";
	if (nav->speech && speech_get_attr(nav->speech, attr_vocabulary_name, &attr, NULL))
		vocabulary1=attr.u.num;
	if (nav->speech && speech_get_attr(nav->speech, attr_vocabulary_name_systematic, &attr, NULL))
		vocabulary2=attr.u.num;
	name=itm->way.name;
	name_systematic=itm->way.name_systematic;
	if (!vocabulary1)
		name=NULL;
	if (!vocabulary2)
		name_systematic=NULL;

	/* Navit now knows the difference between an exit and a ramp towards ...
	 * but if ramp is named it will probaly fail here
	 *
	 *
	 *
	 * */
	if(!name && !name_systematic && itm->way.item.type == type_ramp && vocabulary2) {
			 
		if(next->way.item.type == type_ramp)
			return NULL;
		else
			return g_strdup_printf("%s%s",prefix,_("into the ramp"));

	}

	/* use motorway_like() ? */
	if(!name && !name_systematic && (itm->way.item.type == type_highway_city || itm->way.item.type == type_highway_land)  && vocabulary2) {

		if(next->way.item.type == type_ramp)
			return g_strdup_printf("%s%s",prefix,_("exit"));

	}


	/*despite renaming n1 and n2 stil confusing with name1 and name2*/

	if (!name && !name_systematic)
		return NULL;
	if (name) {
		sex=unknown;
		name1=NULL;
		for (i = 0 ; i < sizeof(suffixes)/sizeof(suffixes[0]) ; i++) {


			if (contains_suffix(name,suffixes[i].fullname)) {
				sex=suffixes[i].sex;
				name1=g_strdup(name);
				break;
			}
			if (contains_suffix(name,suffixes[i].abbrev)) {
				sex=suffixes[i].sex;
				name1=replace_suffix(name, suffixes[i].abbrev, suffixes[i].fullname);
				break;
			}
		}
		if (name_systematic) {
			name2=name_systematic;
			sep=" ";
		} else {
			name2="";
			sep="";
		}
		switch (sex) {
		case unknown:
			/* TRANSLATORS: Arguments: 1: Prefix (Space if required) 2: Street Name 3: Separator (Space if required), 4: Systematic Street Name */
			ret=g_strdup_printf(_("%sinto the street %s%s%s"),prefix,name, sep, name2);
			break;
		case male:
			/* TRANSLATORS: Arguments: 1: Prefix (Space if required) 2: Street Name 3: Separator (Space if required), 4: Systematic Street Name. Male form. The stuff after | doesn't have to be included */
			ret=g_strdup_printf(_("%sinto the %s%s%s|male form"),prefix,name1, sep, name2);
			break;
		case female:
			/* TRANSLATORS: Arguments: 1: Prefix (Space if required) 2: Street Name 3: Separator (Space if required), 4: Systematic Street Name. Female form. The stuff after | doesn't have to be included */
			ret=g_strdup_printf(_("%sinto the %s%s%s|female form"),prefix,name1, sep, name2);
			break;
		case neutral:
			/* TRANSLATORS: Arguments: 1: Prefix (Space if required) 2: Street Name 3: Separator (Space if required), 4: Systematic Street Name. Neutral form. The stuff after | doesn't have to be included */
			ret=g_strdup_printf(_("%sinto the %s%s%s|neutral form"),prefix,name1, sep, name2);
			break;
		}
		g_free(name1);
			
	} else
		/* TRANSLATORS: gives the name of the next road to turn into (into the E17) */
		ret=g_strdup_printf(_("%sinto the %s"),prefix,name_systematic);
	name1=ret;
	while (name1 && *name1) {
		switch (*name1) {
		case '|':
			*name1='\0';
			break;
		case '/':
			*name1++=' ';
			break;
		default:
			name1++;
		}
	}
	return ret;
}

static char *
show_maneuver(struct navigation *nav, struct navigation_itm *itm, struct navigation_command *cmd, enum attr_type type, int connect)
{
	/* TRANSLATORS: right, as in 'Turn right' */
	const char *dir=_("straight"),*strength="";
	int distance=itm->dest_length-cmd->itm->dest_length;
	char *d,*ret=NULL;
	int delta=cmd->delta;
	int level;
	int strength_needed;
	int skip_roads;
	int count_roundabout;
	struct navigation_itm *cur;
	struct navigation_way *w;
	

	/* low priority but slowly move to something like
	 *
	 * if (connect)
	 * 		level = connected;
	 *
	 *
	 *
	 *
	 */


	if (connect) {
		level = -2; /* level = -2 means "connect to another maneuver via 'then ...'" */
	} else {
		level=1;
	}

	w = itm->next->way.next;
	strength_needed = 0;

	/*
	 * hmmmmm
	 *
	 */

	if (angle_delta(itm->next->way.angle2,itm->angle_end) < 0) {
		while (w) {
			if (angle_delta(w->angle2,itm->angle_end) < 0) {
				strength_needed = 1;
				break;
			}
			w = w->next;
		}
	} else {
		while (w) {
			if (angle_delta(w->angle2,itm->angle_end) > 0) {
				strength_needed = 1;
				break;
			}
			w = w->next;
		}
	}


	if (delta > angle_straight) {
			/* TRANSLATORS: right, as in 'Turn right' */
			dir=_("right");
		}
	if (delta < -angle_straight) {
			/* TRANSLATORS: left, as in 'Turn left' */
			dir=_("left");
			delta =-delta;
		}

	if (strength_needed) {
		if (delta < 45 && delta >angle_straight) {
			/* TRANSLATORS: Don't forget the ending space */
			strength=_("easily ");
		} else if (delta < 105) {
			strength="";
		} else if (delta < 165) {
			/* TRANSLATORS: Don't forget the ending space */
			strength=_("strongly ");
		} else if (delta < 180) {
			/* TRANSLATORS: Don't forget the ending space */
			strength=_("really strongly ");
		} else {
			dbg(1,"delta=%d\n", delta);
			/* TRANSLATORS: Don't forget the ending space */
			strength=_("unknown ");
		}
	}
	if (type != attr_navigation_long_exact)
		distance=round_distance(distance);
	if (type == attr_navigation_speech) {
		if (nav->turn_around && nav->turn_around == nav->turn_around_limit) {
			navigation_set_turnaround(nav, nav->turn_around_count+1);
			return g_strdup(_("When possible, please turn around"));
		}
		navigation_set_turnaround(nav, 0);
		if (!connect) {
			level=navigation_get_announce_level_cmd(nav, itm, cmd, distance-cmd->length);
		}
/*		dbg(1,"distance=%d level=%d type=0x%x\n", distance, level, itm->way.item.type); */
	}

	if (cmd->itm->prev->way.flags & AF_ROUNDABOUT) {
		cur = cmd->itm->prev;
		count_roundabout = 0;
		while (cur && (cur->way.flags & AF_ROUNDABOUT)) {
			if (cur->next->way.next && is_way_allowed(nav,cur->next->way.next,3)) { /* If the next segment has no exit or the exit isn't allowed, don't count it */
				count_roundabout++;
			}
			cur = cur->prev;
		}
		switch (level) {
		case 2:
			return g_strdup(_("Enter the roundabout soon"));
		case 1:
			d = get_distance(nav, distance, type, 0);
			/* TRANSLATORS: %s is the distance to the roundabout */
			ret = g_strdup_printf(_("Enter the roundabout %s"), d);
			g_free(d);
			return ret;
		case -2:
			return g_strdup_printf(_("then leave the roundabout at the %s"), get_exit_count_str(count_roundabout));
		case 0:
			return g_strdup_printf(_("Leave the roundabout at the %s"), get_exit_count_str(count_roundabout));
		}
	}

	/*
	 * do we need to expand here for left and right merge ??
	 *
	 * low priority
	 *
	 */

	/* critical but done :) fix a segfault,
	 *
	 * next task : have someone who knows something about computers
	 * check this whole thing out for memory leaks and other pittfalls.
	 *
	 *
	 *
	 */


	if (cmd->reason){  /*hmm  adding this line seems to fix the segfault */
		if (strcmp(cmd->reason,"merge")==0){
			switch (level) {
			case 2:
				return g_strdup(_("merge case 2"));
			case 1:
				return g_strdup_printf(_("merge case 1"));
			case -2:
				return g_strdup_printf(_("then merge case -2"));
			case 0:
				return g_strdup_printf(_("merge case 0"));
			}
		}
	}
	switch(level) {
	case 3:
		d=get_distance(nav, distance, type, 1);
		ret=g_strdup_printf(_("Follow the road for the next %s"), d);
		g_free(d);
		return ret;
	case 2:
		d=g_strdup(_("soon"));
		break;
	case 1:
		d=get_distance(nav, distance, attr_navigation_short, 0);
		break;
	case 0:
		skip_roads = count_possible_turns(nav,cmd->prev?cmd->prev->itm:nav->first,cmd->itm,cmd->delta);
		if (skip_roads > 0 && cmd->itm->next) {
			if (get_count_str(skip_roads+1)) {
				/* TRANSLATORS: First argument is the how manieth street to take, second the direction */ 
				ret = g_strdup_printf(_("Take the %1$s road to the %2$s"), get_count_str(skip_roads+1), dir);
				return ret;
			} else {
				d = g_strdup_printf(_("after %i roads"), skip_roads);
			}
		} else {
			d=g_strdup(_("now"));
		}
		break;
	case -2:
		skip_roads = count_possible_turns(nav,cmd->prev->itm,cmd->itm,cmd->delta);
		if (skip_roads > 0) {
			/* TRANSLATORS: First argument is the how manieth street to take, second the direction */ 
			if (get_count_str(skip_roads+1)) {
				ret = g_strdup_printf(_("then take the %1$s road to the %2$s"), get_count_str(skip_roads+1), dir);
				return ret;
			} else {
				d = g_strdup_printf(_("after %i roads"), skip_roads);
			}

		} else {
			d = g_strdup("");
		}
		break;
	default:

		d=g_strdup(_("error"));
	}
	if (cmd->itm->next) {
		int tellstreetname = 0;
		char *destination = NULL;
 
		if(type == attr_navigation_speech) { /* In voice mode */
			/* In Voice Mode only tell the street name in level 1 or in level 0 if level 1
			 was skipped
			*/

			if (level == 1) { /* we are close to the intersection */
				cmd->itm->streetname_told = 1; // remeber to be checked when we turn
				tellstreetname = 1; // Ok so we tell the name of the street 
			}

			if (level == 0) {
				if(cmd->itm->streetname_told == 0) /* we are right at the intersection */
					tellstreetname = 1; 
				else
					cmd->itm->streetname_told = 0;  /* reset just in case we come to the same street again */
			}

		}
		else
		     tellstreetname = 1;

		if(nav->tell_street_name && tellstreetname)
			destination=navigation_item_destination(nav, cmd->itm, itm, " ");

		if (level != -2) {
			/* TRANSLATORS: The first argument is strength, the second direction, the third distance and the fourth destination Example: 'Turn 'slightly' 'left' in '100 m' 'onto baker street' */
			ret=g_strdup_printf(_("Turn %1$s%2$s %3$s%4$s"), strength, dir, d, destination ? destination:"");
		} else {
			/* TRANSLATORS: First argument is strength, second direction, third how many roads to skip, fourth destination */
			ret=g_strdup_printf(_("then turn %1$s%2$s %3$s%4$s"), strength, dir, d, destination ? destination:"");
		}
		g_free(destination);
	} else {
		if (!connect) {
			ret=g_strdup_printf(_("You have reached your destination %s"), d);
		} else {
			ret=g_strdup(_("then you have reached your destination."));
		}
		if (type == attr_navigation_speech && (nav->flags & 1))
			route_set_destination(nav->route, NULL, 0);
			
	}
	g_free(d);
	return ret;
}

/**
 * @brief Creates announcements for maneuvers, plus maneuvers immediately following the next maneuver
 *
 * This function does create an announcement for the current maneuver and for maneuvers
 * immediately following that maneuver, if these are too close and we're in speech navigation.
 *
 * @return An announcement that should be made
 */
static char *
show_next_maneuvers(struct navigation *nav, struct navigation_itm *itm, struct navigation_command *cmd, enum attr_type type)
{
	struct navigation_command *cur,*prev;
	int distance=itm->dest_length-cmd->itm->dest_length;
	int level, i, time;
	int speech_time,time2nav;
	char *ret,*old,*buf,*next;

	if (type != attr_navigation_speech) {
		return show_maneuver(nav, itm, cmd, type, 0); // We accumulate maneuvers only in speech navigation
	}

	level=navigation_get_announce_level(nav, itm->way.item.type, distance-cmd->length);

	if (level > 1) {
		return show_maneuver(nav, itm, cmd, type, 0); // We accumulate maneuvers only if they are close
	}

	if (cmd->itm->told) {
		return g_strdup("");
	}

	ret = show_maneuver(nav, itm, cmd, type, 0);
	time2nav = navigation_time(itm,cmd->itm->prev);
	old = NULL;

	cur = cmd->next;
	prev = cmd;
	i = 0;
	while (cur && cur->itm) {
		// We don't merge more than 3 announcements...
		if (i > 1) { // if you change this, please also change the value below, that is used to terminate the loop
			break;
		}
		
		next = show_maneuver(nav,prev->itm, cur, type, 0);
		if (nav->speech)
			speech_time = speech_estimate_duration(nav->speech,next);
		else
			speech_time = -1;
		g_free(next);

		if (speech_time == -1) { // user didn't set cps
			speech_time = 30; // assume 3 seconds
		}

		time = navigation_time(prev->itm,cur->itm->prev);

		if (time >= (speech_time + 30)) { // 3 seconds for understanding what has been said
			break;
		}

		old = ret;
		buf = show_maneuver(nav, prev->itm, cur, type, 1);
		ret = g_strdup_printf("%s, %s", old, buf);
		g_free(buf);
		if (nav->speech && speech_estimate_duration(nav->speech,ret) > time2nav) {
			g_free(ret);
			ret = old;
			i = 2; /* This will terminate the loop */
		} else {
			g_free(old);
		}

		/* If the two maneuvers are *really* close, we shouldn't tell the second one again, because TTS won't be fast enough */
		if (time <= speech_time) {
			cur->itm->told = 1;
		}

		prev = cur;
		cur = cur->next;
		i++;
	}

	return ret;
}

static void
navigation_call_callbacks(struct navigation *this_, int force_speech)
{
	int distance, level = 0;
	void *p=this_;
	if (!this_->cmd_first)
		return;
	callback_list_call(this_->callback, 1, &p);
	dbg(1,"force_speech=%d turn_around=%d turn_around_limit=%d\n", force_speech, this_->turn_around, this_->turn_around_limit);
	distance=round_distance(this_->first->dest_length-this_->cmd_first->itm->dest_length);
	if (this_->turn_around_limit && this_->turn_around == this_->turn_around_limit) {
		dbg(1,"distance=%d distance_turn=%d\n", distance, this_->distance_turn);
		while (distance > this_->distance_turn) {
			this_->level_last=4;
			level=4;
			force_speech=2;
			if (this_->distance_turn >= 500)
				this_->distance_turn*=2;
			else
				this_->distance_turn=500;
		}
	} else if (!this_->turn_around_limit || this_->turn_around == -this_->turn_around_limit+1) {
		this_->distance_turn=50;
		distance-=this_->cmd_first->length;
		level=navigation_get_announce_level_cmd(this_, this_->first, this_->cmd_first, distance);
		if (level < this_->level_last) {
			/* only tell if the level is valid for more than 3 seconds */
			int speed_distance=this_->first->speed*30/36;
			if (distance < speed_distance || navigation_get_announce_level_cmd(this_, this_->first, this_->cmd_first, distance-speed_distance) == level) {
				dbg(1,"distance %d speed_distance %d\n",distance,speed_distance);
				dbg(1,"level %d < %d\n", level, this_->level_last);
				this_->level_last=level;
				force_speech=3;
			}
		}
		if (!item_is_equal(this_->cmd_first->itm->way.item, this_->item_last)) {
			this_->item_last=this_->cmd_first->itm->way.item;
			if (this_->delay)
				this_->curr_delay=this_->delay;
			else
				force_speech=5;
		} else {
			if (this_->curr_delay) {
				this_->curr_delay--;
				if (!this_->curr_delay)
					force_speech=4;
			}
		}
	}
	if (force_speech) {
		this_->level_last=level;
		this_->curr_delay=0;
		dbg(1,"force_speech=%d distance=%d level=%d type=0x%x\n", force_speech, distance, level, this_->first->way.item.type);
		callback_list_call(this_->callback_speech, 1, &p);
	}
}

static void
navigation_update(struct navigation *this_, struct route *route, struct attr *attr)
{
	struct map *map;
	struct map_rect *mr;
	struct item *ritem;			/* Holds an item from the route map */
	struct item *sitem;			/* Holds the corresponding item from the actual map */
	struct attr street_item,street_direction;
	struct navigation_itm *itm;
	struct attr vehicleprofile;
	int mode=0, incr=0, first=1;
	if (attr->type != attr_route_status)
		return;

	dbg(1,"enter %d\n", mode);
	if (attr->u.num == route_status_no_destination || attr->u.num == route_status_not_found || attr->u.num == route_status_path_done_new) 
		navigation_flush(this_);
	if (attr->u.num != route_status_path_done_new && attr->u.num != route_status_path_done_incremental)
		return;
		
	if (! this_->route)
		return;
	map=route_get_map(this_->route);
	if (! map)
		return;
	mr=map_rect_new(map, NULL);
	if (! mr)
		return;
	if (route_get_attr(route, attr_vehicleprofile, &vehicleprofile, NULL))
		this_->vehicleprofile=vehicleprofile.u.vehicleprofile;
	else
		this_->vehicleprofile=NULL;
/*	dbg(1,"enter\n"); */
	while ((ritem=map_rect_get_item(mr))) {
		if (ritem->type == type_route_start && this_->turn_around > -this_->turn_around_limit+1)
			this_->turn_around--;
		if (ritem->type == type_route_start_reverse && this_->turn_around < this_->turn_around_limit)
			this_->turn_around++;
		if (ritem->type != type_street_route)
			continue;
		if (first && item_attr_get(ritem, attr_street_item, &street_item)) {
			first=0;
			if (!item_attr_get(ritem, attr_direction, &street_direction))
				street_direction.u.num=0;
			sitem=street_item.u.item;
		/*	dbg(1,"sitem=%p\n", sitem); */
			itm=item_hash_lookup(this_->hash, sitem);
		/*	dbg(2,"itm for item with id (0x%x,0x%x) is %p\n", sitem->id_hi, sitem->id_lo, itm); */
			if (itm && itm->way.dir != street_direction.u.num) {
		/*		dbg(2,"wrong direction\n"); */
				itm=NULL;
			}
			navigation_destroy_itms_cmds(this_, itm);
			if (itm) {
				navigation_itm_update(itm, ritem);
				break;
			}
		/*	dbg(1,"not on track\n"); */
		}
		navigation_itm_new(this_, ritem);
	}
	dbg(2,"turn_around=%d\n", this_->turn_around);
	if (first) 
		navigation_destroy_itms_cmds(this_, NULL);
	else {
		if (! ritem) {
			navigation_itm_new(this_, NULL);
			make_maneuvers(this_,this_->route);
		}
		calculate_dest_distance(this_, incr);
		profile(0,"end");
		navigation_call_callbacks(this_, FALSE);
	}
	map_rect_destroy(mr);
}

static void
navigation_flush(struct navigation *this_)
{
	navigation_destroy_itms_cmds(this_, NULL);
}


void
navigation_destroy(struct navigation *this_)
{
	navigation_flush(this_);
	item_hash_destroy(this_->hash);
	callback_list_destroy(this_->callback);
	callback_list_destroy(this_->callback_speech);
	g_free(this_);
}

int
navigation_register_callback(struct navigation *this_, enum attr_type type, struct callback *cb)
{
	if (type == attr_navigation_speech)
		callback_list_add(this_->callback_speech, cb);
	else
		callback_list_add(this_->callback, cb);
	return 1;
}

void
navigation_unregister_callback(struct navigation *this_, enum attr_type type, struct callback *cb)
{
	if (type == attr_navigation_speech)
		callback_list_remove(this_->callback_speech, cb);
	else
		callback_list_remove(this_->callback, cb);
}

struct map *
navigation_get_map(struct navigation *this_)
{
	struct attr *attrs[5];
	struct attr type,navigation,data,description;
	type.type=attr_type;
	type.u.str="navigation";
	navigation.type=attr_navigation;
	navigation.u.navigation=this_;
	data.type=attr_data;
	data.u.str="";
	description.type=attr_description;
	description.u.str="Navigation";
	
	attrs[0]=&type;
	attrs[1]=&navigation;
	attrs[2]=&data;
	attrs[3]=&description;
	attrs[4]=NULL;
	if (! this_->map)
		this_->map=map_new(NULL, attrs);
        return this_->map;
}

struct map_priv {
	struct navigation *navigation;
};

struct map_rect_priv {
	struct navigation *nav;
	struct navigation_command *cmd;
	struct navigation_command *cmd_next;
	struct navigation_itm *itm;
	struct navigation_itm *itm_next;
	struct navigation_itm *cmd_itm;
	struct navigation_itm *cmd_itm_next;
	struct item item;
	enum attr_type attr_next;
	int ccount;
	int debug_idx;
	struct navigation_way *ways;
	int show_all;
	char *str;
};

static int
navigation_map_item_coord_get(void *priv_data, struct coord *c, int count)
{
	struct map_rect_priv *this=priv_data;
	if (this->ccount || ! count)
		return 0;
	*c=this->itm->start;
	this->ccount=1;
	return 1;
}

static void
navigation_map_item_coord_rewind(void *priv_data)
{
	struct map_rect_priv *this=priv_data;
	this->ccount=0;
}


static int
navigation_map_item_attr_get(void *priv_data, enum attr_type attr_type, struct attr *attr)
{
	struct map_rect_priv *this_=priv_data;
	struct navigation_command *cmd=this_->cmd;
	struct navigation_itm *itm=this_->itm;
	struct navigation_itm *prev=itm->prev;
	attr->type=attr_type;

	if (this_->str) {
		g_free(this_->str);
		this_->str=NULL;
	}

	if (cmd) {
		if (cmd->itm != itm)
			cmd=NULL;	
	}
	switch(attr_type) {
	case attr_level:
		if (cmd) {
			int distance=this_->cmd_itm->dest_length-cmd->itm->dest_length;
			distance=round_distance(distance);
			attr->u.num=navigation_get_announce_level(this_->nav, this_->cmd_itm->way.item.type, distance-cmd->length);
			return 1;
		}
		return 0;
	case attr_navigation_short:
		this_->attr_next=attr_navigation_long;
		if (cmd) {
			this_->str=attr->u.str=show_next_maneuvers(this_->nav, this_->cmd_itm, cmd, attr_type);
			return 1;
		}
		return 0;
	case attr_navigation_long:
		this_->attr_next=attr_navigation_long_exact;
		if (cmd) {
			this_->str=attr->u.str=show_next_maneuvers(this_->nav, this_->cmd_itm, cmd, attr_type);
			return 1;
		}
		return 0;
	case attr_navigation_long_exact:
		this_->attr_next=attr_navigation_speech;
		if (cmd) {
			this_->str=attr->u.str=show_next_maneuvers(this_->nav, this_->cmd_itm, cmd, attr_type);
			return 1;
		}
		return 0;
	case attr_navigation_speech:
		this_->attr_next=attr_length;
		if (cmd) {
			this_->str=attr->u.str=show_next_maneuvers(this_->nav, this_->cmd_itm, this_->cmd, attr_type);
			return 1;
		}
		return 0;
	case attr_length:
		this_->attr_next=attr_time;
		if (cmd) {
			attr->u.num=this_->cmd_itm->dest_length-cmd->itm->dest_length;
			return 1;
		}
		return 0;
	case attr_time:
		this_->attr_next=attr_destination_length;
		if (cmd) {
			attr->u.num=this_->cmd_itm->dest_time-cmd->itm->dest_time;
			return 1;
		}
		return 0;
	case attr_destination_length:
		attr->u.num=itm->dest_length;
		this_->attr_next=attr_destination_time;
		return 1;
	case attr_destination_time:
		attr->u.num=itm->dest_time;
		this_->attr_next=attr_street_name;
		return 1;
	case attr_street_name:
		attr->u.str=itm->way.name;
		this_->attr_next=attr_street_name_systematic;
		if (attr->u.str){
			return 1;}
		return 0;
	case attr_street_name_systematic:
		attr->u.str=itm->way.name_systematic;
		this_->attr_next=attr_street_lanes;
		if (attr->u.str){
			return 1;}
		return 0;
	case attr_street_lanes:
			attr->u.str=itm->way.lanes;
			this_->attr_next=attr_destination;
			if (attr->u.str){
				return 1;}
			return 0;
	case attr_street_destination:
			attr->u.str=itm->way.destination;
			this_->attr_next=attr_debug;
			if (attr->u.str){
				return 1;}
			return 0;

	case attr_debug:
		switch(this_->debug_idx) {
		case 0:
			this_->debug_idx++;
			this_->str=attr->u.str=g_strdup_printf("angle:%d (- %d)", itm->way.angle2, itm->angle_end);
			return 1;
		case 1:
			this_->debug_idx++;
			this_->str=attr->u.str=g_strdup_printf("item type:%s", item_to_name(itm->way.item.type));
			return 1;
		case 2:
			this_->debug_idx++;
			if (cmd) {
				this_->str=attr->u.str=g_strdup_printf("delta:%d", cmd->delta);
				return 1;
			}
		case 3:
			this_->debug_idx++;
			if (prev) {
				this_->str=attr->u.str=g_strdup_printf("prev street_name:%s", prev->way.name);
				return 1;
			}
		case 4:
			this_->debug_idx++;
			if (prev) {
				this_->str=attr->u.str=g_strdup_printf("prev street_name_systematic:%s", prev->way.name_systematic);
				return 1;
			}
		case 5:
			this_->debug_idx++;
			if (prev) {
				this_->str=attr->u.str=g_strdup_printf("prev angle:(%d -) %d", prev->way.angle2, prev->angle_end);
				return 1;
			}
		case 6:
			this_->debug_idx++;
			this_->ways=itm->way.next;
			if (prev) {
				this_->str=attr->u.str=g_strdup_printf("prev item type:%s", item_to_name(prev->way.item.type));
				return 1;
			}
		case 7:
			if (this_->ways && prev) {
				this_->str=attr->u.str=g_strdup_printf("other item angle:%d delta:%d flags:%d dir:%d type:%s id:(0x%x,0x%x)", this_->ways->angle2, angle_delta(prev->angle_end, this_->ways->angle2), this_->ways->flags, this_->ways->dir, item_to_name(this_->ways->item.type), this_->ways->item.id_hi, this_->ways->item.id_lo);
				this_->ways=this_->ways->next;
				return 1;
			}
			this_->debug_idx++;
		case 8:
			this_->debug_idx++;
			if (prev) {
				int delta=0;
				char *reason=NULL;
				maneuver_required2(this_->nav, prev, itm, &delta, &reason);
				this_->str=attr->u.str=g_strdup_printf("reason:%s",reason);
				return 1;
			}
			
		default:
			this_->attr_next=attr_none;
			return 0;
		}
	case attr_any:
		while (this_->attr_next != attr_none) {
			if (navigation_map_item_attr_get(priv_data, this_->attr_next, attr))
				return 1;
		}
		return 0;
	default:
		attr->type=attr_none;
		return 0;
	}
}

static void
navigation_map_item_attr_rewind(void *priv_data)
{
	struct map_rect_priv *priv = priv_data;
	priv->debug_idx=0;
	priv->attr_next=attr_navigation_short;
}

static struct item_methods navigation_map_item_methods = {
	navigation_map_item_coord_rewind,
	navigation_map_item_coord_get,
	navigation_map_item_attr_rewind,
	navigation_map_item_attr_get,
};


static void
navigation_map_destroy(struct map_priv *priv)
{
	g_free(priv);
}

static void
navigation_map_rect_init(struct map_rect_priv *priv)
{
	priv->cmd_next=priv->nav->cmd_first;
	priv->cmd_itm_next=priv->itm_next=priv->nav->first;
}

static struct map_rect_priv *
navigation_map_rect_new(struct map_priv *priv, struct map_selection *sel)
{
	struct navigation *nav=priv->navigation;
	struct map_rect_priv *ret=g_new0(struct map_rect_priv, 1);
	ret->nav=nav;
	navigation_map_rect_init(ret);
	ret->item.meth=&navigation_map_item_methods;
	ret->item.priv_data=ret;
#ifdef DEBUG
	ret->show_all=1;
#endif
	return ret;
}

static void
navigation_map_rect_destroy(struct map_rect_priv *priv)
{
	g_free(priv->str);
	g_free(priv);
}

/* good discription needed */
static struct item *
navigation_map_get_item(struct map_rect_priv *priv)
{
	struct item *ret=&priv->item;
	int delta;
	if (!priv->itm_next)
		return NULL;
	priv->itm=priv->itm_next;
	priv->cmd=priv->cmd_next;
	priv->cmd_itm=priv->cmd_itm_next;
	if (!priv->cmd)
		return NULL;
	if (!priv->show_all && priv->itm->prev != NULL) 
		priv->itm=priv->cmd->itm;
	priv->itm_next=priv->itm->next;
	if (priv->itm->prev)
		ret->type=type_nav_none;
	else
		ret->type=type_nav_position;
	if (priv->cmd->itm == priv->itm) {
		priv->cmd_itm_next=priv->cmd->itm;
		priv->cmd_next=priv->cmd->next;
		if (priv->cmd_itm_next && !priv->cmd_itm_next->next)
			ret->type=type_nav_destination;
		else {
			if (priv->itm && priv->itm->prev && !(priv->itm->way.flags & AF_ROUNDABOUT) && (priv->itm->prev->way.flags & AF_ROUNDABOUT)) {

				/* code suggests it picks the correct icon, but fails to in many cases */

				enum item_type r=type_none,l=type_none;
				switch (((180+22)-priv->cmd->roundabout_delta)/45) {
				case 0:
				case 1:
					r=type_nav_roundabout_r1;
					l=type_nav_roundabout_l7;
					break;
				case 2:
					r=type_nav_roundabout_r2;
					l=type_nav_roundabout_l6;
					break;
				case 3:
					r=type_nav_roundabout_r3;
					l=type_nav_roundabout_l5;
					break;
				case 4:
					r=type_nav_roundabout_r4;
					l=type_nav_roundabout_l4;
					break;
				case 5:
					r=type_nav_roundabout_r5;
					l=type_nav_roundabout_l3;
					break;
				case 6:
					r=type_nav_roundabout_r6;
					l=type_nav_roundabout_l2;
					break;
				case 7:
					r=type_nav_roundabout_r7;
					l=type_nav_roundabout_l1;
					break;
				case 8:
					r=type_nav_roundabout_r8;
					l=type_nav_roundabout_l8;
					break;
				}
				dbg(1,"delta %d\n",priv->cmd->delta);
				if (priv->cmd->delta < 0)
					ret->type=l;
				else
					ret->type=r;
			} else { /*TODO turn all these angles into constants with SPD for speech as well*/
				delta=priv->cmd->delta;	
				if (delta < -angle_straight) {
					int absdelta=-delta;
					if (absdelta < 45)
						ret->type=type_nav_left_1;
					else if (absdelta < 105)
						ret->type=type_nav_left_2;
					else if (absdelta < 165)
						ret->type=type_nav_left_3;
					else
					 /*	ret->type=type_none; */
						/* nice try but not good enough */
						ret->type=type_nav_turnaround_left;
				} else {
					if (delta < angle_straight )
						ret->type=type_nav_straight;
					else if (delta < 45)
						ret->type=type_nav_right_1;
					else if (delta < 105)
						ret->type=type_nav_right_2;
					else if (delta < 165) 
						ret->type=type_nav_right_3;
					else
					/*	ret->type=type_none; */
						/* nice try but not good enough */
						ret->type=type_nav_turnaround_right;
				}
			}
			if (strcmp(priv->cmd->reason,"merge")==0)
			{
				/*defaults to left for delta = 0
				 *
				 * I don't think you can technically merge with zero degrees
				 *
				 */


				if (delta < 0)
				ret->type=type_nav_merge_right;
				else ret->type=type_nav_merge_left;
			}
			if (strcmp(priv->cmd->reason,"exit")==0)
			{
				/* defaults to right for delta=0
				 * to my best knowledge this is good for now
				 * a zero degree exit seems to hold a contradiction in it.
				 *
				 */
				if (delta < 0)
				ret->type=type_nav_exit_left;
				else ret->type=type_nav_exit_right;
			}
		}
	}
	navigation_map_item_coord_rewind(priv);
	navigation_map_item_attr_rewind(priv);

	ret->id_lo=priv->itm->dest_count;
	dbg(1,"type=%d\n", ret->type);
	return ret;
}

static struct item *
navigation_map_get_item_byid(struct map_rect_priv *priv, int id_hi, int id_lo)
{
	struct item *ret;
	navigation_map_rect_init(priv);
	while ((ret=navigation_map_get_item(priv))) {
		if (ret->id_hi == id_hi && ret->id_lo == id_lo) 
			return ret;
	}
	return NULL;
}

static struct map_methods navigation_map_meth = {
	projection_mg,
	"utf-8",
	navigation_map_destroy,
	navigation_map_rect_new,
	navigation_map_rect_destroy,
	navigation_map_get_item,
	navigation_map_get_item_byid,
	NULL,
	NULL,
	NULL,
};

static struct map_priv *
navigation_map_new(struct map_methods *meth, struct attr **attrs, struct callback_list *cbl)
{
	struct map_priv *ret;
	struct attr *navigation_attr;

	navigation_attr=attr_search(attrs, NULL, attr_navigation);
	if (! navigation_attr)
		return NULL;
	ret=g_new0(struct map_priv, 1);
	*meth=navigation_map_meth;
	ret->navigation=navigation_attr->u.navigation;

	return ret;
}

void
navigation_set_route(struct navigation *this_, struct route *route)
{
	struct attr callback;
	if (!this_->route_cb)
		this_->route_cb=callback_new_attr_1(callback_cast(navigation_update), attr_route_status, this_);
	callback.type=attr_callback;
	callback.u.callback=this_->route_cb;
	if (this_->route)
		route_remove_attr(this_->route, &callback);
	this_->route=route;
	if (this_->route) {
		struct attr route_status;
		route_add_attr(this_->route, &callback);
		if (route_get_attr(this_->route, attr_route_status, &route_status, NULL))
			navigation_update(this_, this_->route, &route_status);
	}
}

void
navigation_init(void)
{
	plugin_register_map_type("navigation", navigation_map_new);
}

struct object_func navigation_func = {
	attr_navigation,
	(object_func_new)navigation_new,
	(object_func_get_attr)navigation_get_attr,
	(object_func_iter_new)navit_object_attr_iter_new,
	(object_func_iter_destroy)navit_object_attr_iter_destroy,
	(object_func_set_attr)navigation_set_attr,
	(object_func_add_attr)navit_object_add_attr,
	(object_func_remove_attr)navit_object_remove_attr,
	(object_func_init)NULL,
	(object_func_destroy)navigation_destroy,
	(object_func_dup)NULL,
	(object_func_ref)navit_object_ref,
	(object_func_unref)navit_object_unref,
};

