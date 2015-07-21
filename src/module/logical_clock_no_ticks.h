
#ifndef LOGICAL_CLOCK_H
#define LOGICAL_CLOCK_H

/*

  Copyright (c) 2012-15 Tim Merrifield, University of Illinois at Chicago


  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/


#define logical_clock_update_clock_ticks(group_info, tid)

#define logical_clock_read_clock_and_update(group_info,id)

#define logical_clock_reset_current_ticks(group_info, id)

#define logical_clock_update_overflow_period(group_info, id)

#define logical_clock_reset_overflow_period(group_info, id)

#define logical_clock_set_perf_counter_max(group_info, id)

#define logical_clock_set_perf_counter(group_info, id)

#endif
