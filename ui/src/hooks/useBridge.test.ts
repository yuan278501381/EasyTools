/* @vitest-environment jsdom */

import { describe, expect, it } from 'vitest';
import { bridgeRequest } from './useBridge';

describe('useBridge Single-Flight Deduplication', () => {
  it('deduplicates concurrent in-flight requests for general.getSettings', async () => {
    // 模拟并发调用：在同一个微任务周期内同时发起两次 general.getSettings
    const p1 = bridgeRequest('general.getSettings');
    const p2 = bridgeRequest('general.getSettings');

    // 关键断言：两次调用必须返回同一个在途 Promise 单飞锁实例
    expect(p1).toBe(p2);

    const [res1, res2] = await Promise.all([p1, p2]);
    expect(res1).toBeDefined();
    expect(res2).toBeDefined();
    expect(res1).toEqual(res2);

    // 请求完成后单飞锁重置，随后的新调用产生新的独立 Promise 实例
    const p3 = bridgeRequest('general.getSettings');
    expect(p3).not.toBe(p1);
    const res3 = await p3;
    expect(res3).toBeDefined();
  });

  it('does not deduplicate different methods', async () => {
    const p1 = bridgeRequest('capture.getSettings');
    const p2 = bridgeRequest('capture.getSettings');
    // capture.getSettings 未走 single-flight 锁，生成独立请求
    expect(p1).not.toBe(p2);
    await Promise.all([p1, p2]);
  });
});
