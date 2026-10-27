#!/usr/bin/env python3
"""CLI contracts: strict inputs, explicit unsupported modes and rectangular exports."""
import csv
import json
import math
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'rf-workbench'

def run(*args, ok=True):
    result = subprocess.run([str(EXE), *map(str, args)], capture_output=True, text=True)
    assert (result.returncode == 0) == ok, (args, result.returncode, result.stdout, result.stderr)
    return result

for flag in ('--frequency', '--density', '--field', '--kmin', '--kmax', '--kz', '--zmin', '--zmax'):
    for value in ('abc', 'NaN', 'Inf', '1abc', '1e999'):
        run('--check', flag, value, ok=False)
for flag in ('--m', '--radial-cells', '--kpoints', '--zpoints', '--rpoints'):
    for value in ('abc', '1.5', '4294967297', '-999999999999999999'):
        run('--check', flag, value, ok=False)
for args in (('--frequency', '-1'), ('--density', '-1'), ('--radial-cells', '0'),
             ('--kpoints', '0'), ('--m', '17'), ('--kmin', '40', '--kmax', '-40')):
    run('--check', *args, ok=False)
check = json.loads(run('--check', '--frequency', '25', '--m', '2', '--kpoints', '7').stdout)
assert check['frequency_mhz'] == 25 and check['nk'] == 7
for flag in ('--plasma-suite', '--plasma-scan'):
    run(flag, ok=False)
run('--plasma', '--plasma-method', 'limiting')
run('--plasma-resonance')
run('--plasma', '--plasma-method', 'nonsense', ok=False)
run('--gui', '--frequency', '10', ok=False)
# Both are symmetry-cancelled modes and used to produce a false backward-error failure.
run('--plasma', '--project', ROOT.parent/'examples'/'07_saddle.rfproj', '--m', '0')
run('--vacuum', '--project', ROOT.parent/'examples'/'04_loop.rfproj', '--m', '1')
for path in sorted((ROOT.parent / 'examples').glob('*.rfproj')):
    run('--check', '--project', path)

with tempfile.TemporaryDirectory(prefix='rf-cli-test-') as tmp:
    tmp = Path(tmp)
    project = tmp / 'project.rfproj'
    run('--save-default', project)
    for fields in (True, False):
        target = tmp / ('fields.csv' if fields else 'source.csv')
        args = ['--scan', '--m', '0', '--kpoints', '3', '--output', target]
        if not fields:
            args += ['--source-only']
        run(*args)
        with target.open() as f:
            rows = list(csv.DictReader(f))
        assert len(rows) == (3 * 257 if fields else 3)
        for row in rows:
            assert None not in row and all(v is not None for v in row.values())
            assert row['valid'] == '1' and row['error'] == ''
            x=float(row['kz_rad_per_m'])*.02/2
            expected=math.sin(x)/x if x else 1
            assert abs(float(row['source_phi_re'])-expected)<1e-12
            if fields:
                assert math.isfinite(float(row['total_V']))
            else:
                assert row['r_m'] == '' and math.isnan(float(row['backward_error']))
    # A geometrically invalid source must not be replaced with a guessed one.
    bad = tmp / 'invalid_geometry.rfproj'
    bad.write_text('\n'.join('type=1' if line.startswith('type=') else 'width=0.001' if line.startswith('width=') else line for line in project.read_text().splitlines())+'\n')
    assert 'width=0.001' in bad.read_text()
    failed_csv = tmp / 'failed.csv'
    run('--scan', '--project', bad, '--kpoints', '3', '--output', failed_csv, ok=False)
    with failed_csv.open() as f:
        rows = list(csv.DictReader(f))
    assert rows and all(row['valid'] == '0' and row['error'] for row in rows)
    assert all(math.isnan(float(row['total_V'])) for row in rows)
    for kind in ('--vacuum', '--plasma'):
        target = tmp / (kind[2:] + '.csv')
        run(kind, '--output', target)
        with target.open() as f:
            rows = list(csv.DictReader(f))
        assert len(rows) == 257 and all(None not in row for row in rows)
        svg = tmp / (kind[2:] + '.svg')
        run(kind, '--output', svg)
        svg_root = ET.parse(svg).getroot()
        assert svg_root.tag.endswith('svg')
        labels = [element.text for element in svg_root.iter() if element.tag.endswith('text')]
        assert '|E_hat| [V]' in labels and 'r [m]' in labels
        field_path = svg_root.find('{http://www.w3.org/2000/svg}path')
        coordinates = list(map(float, field_path.attrib['d'].replace('M', ' ').replace('L', ' ').split()))
        maximum = max(float(row['total_V']) for row in rows) or 1.
        r0, r1 = float(rows[0]['r_m']), float(rows[-1]['r_m'])
        for index in (0, len(rows)//2, len(rows)-1):
            radius, field = float(rows[index]['r_m']), float(rows[index]['total_V'])
            assert abs(coordinates[2*index]-(75+790*(radius-r0)/(r1-r0))) < .0006
            assert abs(coordinates[2*index+1]-(455-393*field/maximum)) < .0006
    # Spatial exports contain physical fields; impedance is a finite-band, one-m contribution.
    space = tmp / 'space.csv'
    impedance = tmp / 'impedance.csv'
    args = ['--impedance', '--radial-cells', '64', '--nk', '5', '--kmin', '-6', '--kmax', '6',
            '--zmin', '-.5', '--zmax', '.5', '--zpoints', '17', '--rpoints', '17']
    summary = json.loads(run(*args, '--output', space, '--spectrum', impedance).stdout)
    assert summary['window_power_W'] > 0 and summary['band_absorbed_W'] > 0
    assert summary['power_balance_error'] < 1e-8
    assert abs(summary['R_ohm'] - 2*summary['band_source_W']) < 1e-10
    with space.open() as f:
        rows = list(csv.DictReader(f))
    assert len(rows) == 17*17 and all(None not in row for row in rows)
    for row in rows:
        assert float(row['q_W_m3']) >= 0 and all(math.isfinite(float(v)) for v in row.values())
        work = sum(complex(float(row[e+'_re_V_m']),float(row[e+'_im_V_m'])).conjugate() *
                   complex(float(row[j+'_re_A_m2']),float(row[j+'_im_A_m2']))
                   for e,j in [('Er','Jr'),('Ephi','Jphi'),('Ez','Jz')]).real/2
        assert math.isclose(work,float(row['q_W_m3']),rel_tol=1e-8,abs_tol=1e-15)
    with impedance.open() as f:
        rows = list(csv.DictReader(f))
    assert len(rows)==5 and all(None not in row for row in rows)
    R=sum(float(r['R_ohm_m'])*float(r['dk_rad_per_m']) for r in rows)
    assert math.isclose(R,summary['R_ohm'],rel_tol=1e-12)
    assert math.isclose(float(rows[0]['kz_rad_per_m']),-4.8)
    run(*args,'--plasma-method','limiting',ok=False)
    run(*args,'--zmax','99',ok=False)
    run(*args,'--zpoints','3','--kmin','-100','--kmax','100',ok=False)
    run(*args,'--output',tmp/'bad.svg',ok=False)
    # Full local branches retain all four roots, exclusions and ordered ray points.
    branches = tmp / 'branches.csv'
    rays = tmp / 'rays.csv'
    example = ROOT.parent / 'examples' / '全分支螺旋波示例.rfproj'
    run('--branches', '--project', example, '--output', branches, '--rays', rays)
    with branches.open() as f:
        branch_rows = list(csv.DictReader(f))
    assert len(branch_rows) == 4*(512+3)
    assert all(None not in row and all(v is not None for v in row.values()) for row in branch_rows)
    assert {(r['branch'],r['kr_sign']) for r in branch_rows} == {('A','1'),('A','-1'),('B','1'),('B','-1')}
    assert any(r['state']=='evanescent' and r['ray_id']=='0' for r in branch_rows)
    with rays.open() as f:
        ray_rows = list(csv.DictReader(f))
    assert {r['ray_id'] for r in ray_rows} == {'1','2','3','4'}
    last = {}
    for row in ray_rows:
        ident = row['ray_id']; t = float(row['ray_time_s'])
        assert t >= last.get(ident,0) and row['ray_eligible']=='1'
        assert float(row['power_optical_depth']) >= -1e-12
        assert row['region']=='plasma'
        assert float(row['frequency_mhz'])==13.56 and row['m']=='1' and float(row['kz_rad_m'])==10
        last[ident] = t
    assert sum(bool(r['ray_stop']) for r in ray_rows)==4
    assert all(None not in r and all(v is not None for v in r.values()) for r in ray_rows)
    for ident in {'1','2','3','4'}:
        start=next(r for r in ray_rows if r['ray_id']==ident)
        assert float(start['r_m'])==.2 and float(start['ray_z_m'])==0
    run('--branches','--project',example,'--ray-r','.2','--ray-z','1.25','--ray-phi','45',
        '--ray-direction','inward','--ray-step','.04','--ray-length','.2','--ray-tol','1e-7','--rays',rays)
    with rays.open() as f:
        launched=list(csv.DictReader(f))
    assert {r['ray_id'] for r in launched}=={'1','2'}
    for ident in {'1','2'}:
        rr=[r for r in launched if r['ray_id']==ident]
        assert float(rr[0]['ray_z_m'])==1.25 and abs(float(rr[0]['ray_phi_rad'])-math.pi/4)<1e-14
        assert float(rr[0]['vg_r_m_s'])<0 and abs(float(rr[-1]['path_m'])-.2)<1e-10
        assert max(float(r['dispersion_residual']) for r in rr)<1e-6
    run('--branches','--project',example,'--ray-r','.0001','--rays',rays)
    with rays.open() as f:
        assert list(csv.DictReader(f))==[]
    for args in [('--ray-r','.7'),('--ray-r','.3'),('--ray-tol','0'),('--ray-step','-1'),('--ray-max-steps','0')]:
        run('--branches','--project',example,*args,ok=False)
    run('--check','--ray-r','.2',ok=False)
    run('--branches', '--project', example, '--output', tmp/'bad.svg', ok=False)
    run('--rays', rays, '--check', ok=False)
    target = tmp / 'collisionless.csv'
    run('--plasma', '--plasma-method', 'collisionless', '--output', target)
    with target.open() as f:
        assert all(float(row['absorption_W_per_m']) == 0 for row in csv.DictReader(f))
print('CLI PASS: strict inputs, examples, CSV/SVG, spatial fields/material work, midpoint spectrum, impedance/power and unsupported limits')
