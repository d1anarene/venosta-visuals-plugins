#!/usr/bin/env python3
from check_probe_log import check

def row(t, neighbour):
    return dict(schema='vv-temporal-probe-v1', action='render', host_path='ofx-cpu', time=t,
                requested_neighbour_time=t-1, scale_x=1,scale_y=1,draft=False,interactive=False,
                format='RGBA-float32-bottom-up', width=3,height=2,current_hash=f'frame{t}',
                neighbour_hash=neighbour,neighbour_available=True,neighbour_width=3,neighbour_height=2,
                bounds_x=0,bounds_y=0,neighbour_bounds_x=0,neighbour_bounds_y=0,same_bounds=True)

records=[row(1,'frame0'),row(2,'frame1')]
assert check(records)['compared']==1
assert check(records)['missing_reference']==1
records[1]['neighbour_hash']='wrong-upstream'
assert check(records)['mismatches']==1
records[1]['neighbour_available']=False
assert check(records)['unavailable_neighbours']==1
records=[row(1,'frame0'),row(1,'frame0')]
records[1]['current_hash']='order-dependent'
assert check(records)['inconsistent_current']==1
print('probe log checker: matching, missing evidence, upstream mismatch, unavailable and nondeterministic inputs detected')

records=[row(1,'frame0'),row(2,'frame1')]
records[1]['same_bounds']=False
assert check(records)['geometry_mismatches']==1
records[1]['same_bounds']=True
records[1]['neighbour_width']=4
assert check(records)['geometry_mismatches']==1
records[1]['neighbour_width']=3
records[1]['neighbour_bounds_x']=7
assert check(records)['geometry_mismatches']==1
# Different current origins must not be compared as if they share a canvas.
records=[row(1,'frame0'),row(2,'frame1')]
records[0]['bounds_x']=7
records[0]['neighbour_bounds_x']=7
assert check(records)['compared']==0
assert check(records)['missing_reference']==2
print('probe log checker: dimensions, explicit bounds failure and origin mismatch rejected')
