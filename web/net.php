<?php
// Tiny presence/chat relay for the Digimon World web port (no game data).
// POST JSON {id,name,map,x,y,z,rot,anim,say,emo,since} -> {players,chat,seq,online}
header('Content-Type: application/json');
header('Cache-Control: no-store');

$raw = file_get_contents('php://input', false, null, 0, 4096);
$in = json_decode($raw, true);
if (!is_array($in)) { echo '{"error":"bad request"}'; exit; }

$id = isset($in['id']) ? (string)$in['id'] : '';
if (!preg_match('/^[a-f0-9]{8,16}$/', $id)) { echo '{"error":"bad id"}'; exit; }

function clean($s, $n) {
    $s = preg_replace('/[^\x20-\x7e\x{00a1}-\x{024f}]/u', '', (string)$s);
    return trim(mb_substr($s, 0, $n));
}

$dirs = array(
    dirname($_SERVER['DOCUMENT_ROOT']) . '/dw_net',
    sys_get_temp_dir() . '/dw_net_' . md5(__DIR__),
);
$dir = null;
foreach ($dirs as $d) {
    if ((is_dir($d) || @mkdir($d, 0700, true)) && is_writable($d)) { $dir = $d; break; }
}
if ($dir === null) { echo '{"error":"no storage"}'; exit; }

$fh = fopen($dir . '/state.json', 'c+');
if (!$fh || !flock($fh, LOCK_EX)) { echo '{"error":"lock"}'; exit; }
$st = json_decode(stream_get_contents($fh), true);
if (!is_array($st)) $st = array('players' => array(), 'chat' => array(), 'seq' => 0);

$now = time();
foreach ($st['players'] as $pid => $p) {
    if ($now - $p['t'] > 12) unset($st['players'][$pid]);
}

$map = (int)($in['map'] ?? -1);
$name = clean($in['name'] ?? '', 16);
if ($name === '') $name = 'Tamer';
if (count($st['players']) < 64 || isset($st['players'][$id])) {
    $old = $st['players'][$id] ?? array('lastsay' => 0);
    $st['players'][$id] = array(
        'id' => $id, 'name' => $name, 'map' => $map,
        'x' => (int)($in['x'] ?? 0), 'y' => (int)($in['y'] ?? 0),
        'z' => (int)($in['z'] ?? 0), 'rot' => (int)($in['rot'] ?? 0),
        'anim' => (int)($in['anim'] ?? 0), 't' => $now,
        'lastsay' => $old['lastsay'],
    );
    $say = clean($in['say'] ?? '', 80);
    $emo = (int)($in['emo'] ?? 0);
    if (($say !== '' || $emo > 0) && $now - $st['players'][$id]['lastsay'] >= 1) {
        $st['players'][$id]['lastsay'] = $now;
        $st['seq']++;
        $st['chat'][] = array('seq' => $st['seq'], 'id' => $id, 'name' => $name,
            'map' => $map, 'say' => $say, 'emo' => max(0, min(9, $emo)), 't' => $now);
        $st['chat'] = array_slice($st['chat'], -40);
    }
}

$since = (int)($in['since'] ?? -1);
if ($since < 0) $since = max(0, $st['seq'] - 8);
$chat = array();
foreach ($st['chat'] as $c) { if ($c['seq'] > $since) $chat[] = $c; }

$players = array();
foreach ($st['players'] as $pid => $p) {
    if ($pid === $id) continue;
    $players[] = array('id' => $p['id'], 'name' => $p['name'], 'map' => $p['map'],
        'x' => $p['x'], 'y' => $p['y'], 'z' => $p['z'], 'rot' => $p['rot'], 'anim' => $p['anim']);
}

ftruncate($fh, 0);
rewind($fh);
fwrite($fh, json_encode($st));
fflush($fh);
flock($fh, LOCK_UN);
fclose($fh);

echo json_encode(array('players' => $players, 'chat' => $chat, 'seq' => $st['seq'],
    'online' => count($st['players'])));
