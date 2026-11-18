'use strict';
let controlToken = '', configuration, timer, sending = false;
const el = id => document.getElementById(id);
function notice(text, error = false) { el('notice').textContent = text; el('notice').className = error ? 'error' : ''; }
async function api(path, body) {
  const response = await fetch(path, {method: body ? 'POST' : 'GET', headers: {'Authorization': 'Bearer ' + controlToken, 'Content-Type': 'application/json'}, body: body ? JSON.stringify(body) : undefined, cache: 'no-store'});
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || '命令未确认，请查看执行结果；不要连续重复提交。');
  return data;
}
function option(select, value, text) { const item = document.createElement('option'); item.value = value; item.textContent = text; select.append(item); }
function targets() {
  const scope = el('scope').value, target = el('target'); target.replaceChildren();
  if (scope === 'device') configuration.devices.forEach(d => option(target, d.id, d.name + ' · ' + d.id));
  if (scope === 'group') configuration.groups.forEach(g => option(target, g, g));
  if (scope === 'all') option(target, '', '本部署全部设备');
  el('send').disabled = sending || !scope || !configuration.actions.length;
}
async function refresh() {
  try {
    const data = await api('/api/state');
    el('broker').textContent = data.broker_ready ? 'MQTT 已连接，订阅已确认' : 'MQTT 未就绪：' + data.broker_error;
    el('devices').replaceChildren();
    configuration.devices.forEach(device => {
      const state = data.devices[device.id], row = document.createElement('tr');
      const values = [device.name + ' (' + device.id + ')', device.group, state.last_seen ? new Date(state.last_seen * 1000).toLocaleTimeString() : '尚无消息', configuration.features.monitor ? (state.state === null ? '等待实板上报' : JSON.stringify(state.state)) : '本部署已关闭状态查看'];
      values.forEach(value => { const cell = document.createElement('td'); cell.textContent = value; row.append(cell); }); el('devices').append(row);
    });
    el('commands').replaceChildren();
    [...data.commands].reverse().forEach(command => {
      const card = document.createElement('article'), title = document.createElement('strong'), detail = document.createElement('pre');
      const success = command.expected.filter(id => command.acks[id]?.code === 200), failed = command.expected.filter(id => command.acks[id] && command.acks[id].code !== 200), waiting = command.expected.filter(id => !command.acks[id]);
      title.textContent = command.id + ' · ' + command.topic;
      detail.textContent = 'MQTT：' + (command.broker_accepted ? '已确认' : '未确认') + (command.error ? ' / ' + command.error : '') + '\n执行成功：' + (success.join(', ') || '暂无') + '\n设备拒绝：' + (failed.join(', ') || '暂无') + '\n待确认：' + (waiting.join(', ') || '无');
      card.append(title, detail); el('commands').append(card);
    });
  } catch (error) { notice(error.message, true); }
}
el('connect').addEventListener('click', async () => {
  controlToken = el('token').value;
  try {
    configuration = await api('/api/config'); el('scope').replaceChildren(); el('action').replaceChildren();
    if (configuration.features.individual) option(el('scope'), 'device', '单独控制');
    if (configuration.features.group) option(el('scope'), 'group', '小组控制');
    if (configuration.features.broadcast) option(el('scope'), 'all', '全班广播');
    configuration.actions.forEach(action => option(el('action'), action.id, action.name));
    el('project').textContent = configuration.project; el('policy').textContent = el('scope').options.length ? '仅显示部署者启用的功能；关闭功能的请求也会被服务端拒绝。' : '当前为只读部署，控制功能全部关闭。';
    targets(); el('workspace').hidden = false; el('login').hidden = true; el('token').value = '';
    notice('控制台已就绪。先确认 MQTT 状态和实板上报。');
    clearInterval(timer); await refresh(); timer = setInterval(refresh, 2000);
  } catch (error) { controlToken = ''; notice(error.message, true); }
});
el('scope').addEventListener('change', targets);
el('send').addEventListener('click', async () => {
  if (sending) return;
  if (el('scope').value !== 'device' && !confirm('将向所选范围内的多块设备发送命令，是否继续？')) return;
  sending = true; targets();
  try { const result = await api('/api/command', {scope: el('scope').value, target: el('target').value, action: el('action').value}); notice('命令已发送：' + result.id + '。请查看逐板回执。'); }
  catch (error) { notice(error.message, true); }
  finally { sending = false; targets(); await refresh(); }
});
