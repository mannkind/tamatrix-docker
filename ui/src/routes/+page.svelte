<script lang="ts">
	import { tamaStore, type TamaData } from '$lib/tamaStore.js';
	import TamaDisplay from '$lib/TamaDisplay.svelte';

	// Small display: scale=5 → 240×160. Big display: scale=15 → 720×480.
	const SMALL_SCALE = 5;
	const BIG_SCALE = 15;

	const ICON_NAMES: [number, string][] = [
		[1 << 0, 'Info'],
		[1 << 1, 'Food'],
		[1 << 2, 'Toilet'],
		[1 << 3, 'Doors'],
		[1 << 4, 'Figure'],
		[1 << 5, 'Training'],
		[1 << 6, 'Medical'],
		[1 << 7, 'IR'],
		[1 << 8, 'Album'],
		[1 << 9, 'Attention'],
	];

	function activeIcons(icons: number): string {
		const active = ICON_NAMES.filter(([bit]) => icons & bit).map(([, name]) => name);
		return active.length ? active.join(', ') : '—';
	}

	let selected: TamaData | null = $state(null);

	function open(t: TamaData) {
		selected = t;
	}

	function close() {
		selected = null;
	}

	async function pressBtn(id: number, btn: number) {
		await fetch(`/presstama.php?id=${id}&btn=${btn}`, { method: 'POST' });
	}

	function handleKeydown(e: KeyboardEvent) {
		if (e.key === 'Escape') close();
	}

	// Derive sorted array from store map
	let tamas = $derived([...$tamaStore.tamas.values()].sort((a, b) => a.id - b.id));
	// Keep the modal display in sync when pixels update
	let selectedLive = $derived(selected ? ($tamaStore.tamas.get(selected.id) ?? selected) : null);
</script>

<svelte:window onkeydown={handleKeydown} />

<main>
	<header>
		<h1>Tamatrix</h1>
		<p>{$tamaStore.notama} Tamagotchi{$tamaStore.notama === 1 ? '' : 's'} running</p>
		<nav>
			<a href="http://spritesmods.com/?art=tamahive" target="_blank" rel="noopener noreferrer">Technical background</a>
			<a href="https://spritesmods.com/?art=contact&af=TamaHive" target="_blank" rel="noopener noreferrer">Contact</a>
		</nav>
	</header>

	{#if tamas.length === 0}
		<div class="empty">Connecting to tamagotchis&hellip;</div>
	{:else}
		<div class="grid">
			{#each tamas as tama (tama.id)}
				<button class="card" onclick={() => open(tama)} title="Tamagotchi #{tama.id}">
					<TamaDisplay pixels={tama.pixels} scale={SMALL_SCALE} />
					<div class="label">{activeIcons(tama.icons)}</div>
				</button>
			{/each}
		</div>
	{/if}
</main>

{#if selectedLive}
	<div class="overlay" onclick={close} role="dialog" aria-modal="true" aria-label="Zoomed view">
		<div class="modal" onclick={(e) => e.stopPropagation()}>
			<TamaDisplay pixels={selectedLive.pixels} scale={BIG_SCALE} />
			<div class="modal-label">{activeIcons(selectedLive.icons)}</div>
			<div class="controls">
				<button class="btn" onclick={() => pressBtn(selectedLive!.id, 0)}>A</button>
				<button class="btn" onclick={() => pressBtn(selectedLive!.id, 1)}>B</button>
				<button class="btn" onclick={() => pressBtn(selectedLive!.id, 2)}>C</button>
			</div>
			<button class="close" onclick={close} aria-label="Close">&times;</button>
		</div>
	</div>
{/if}

<style>
	main {
		padding: 2rem;
	}

	header {
		text-align: center;
		margin-bottom: 2rem;
	}

	header h1 {
		font-size: 2.5rem;
		font-weight: 700;
		letter-spacing: 0.1em;
		color: #a0c8a0;
	}

	header p {
		font-size: 0.9rem;
		color: #888;
		margin-top: 0.25rem;
	}

	header nav {
		display: flex;
		gap: 1.5rem;
		justify-content: center;
		margin-top: 0.75rem;
	}

	header nav a {
		font-size: 0.85rem;
		color: #6aaa6a;
		text-decoration: none;
	}

	header nav a:hover {
		text-decoration: underline;
	}

	.empty {
		text-align: center;
		color: #666;
		font-size: 1.1rem;
		margin-top: 4rem;
	}

	.grid {
		display: flex;
		flex-wrap: wrap;
		gap: 1rem;
		justify-content: center;
	}

	.card {
		background: #16213e;
		border: 1px solid #2a4a2a;
		border-radius: 8px;
		padding: 0.5rem;
		cursor: pointer;
		transition: border-color 0.15s, transform 0.15s;
		line-height: 0;
	}

	.label {
		line-height: normal;
		margin-top: 0.35rem;
		font-size: 0.72rem;
		color: #a0c8a0;
		text-align: center;
		white-space: nowrap;
		overflow: hidden;
		text-overflow: ellipsis;
		max-width: 240px;
	}

	.card:hover {
		border-color: #6aaa6a;
		transform: scale(1.03);
	}

	.overlay {
		position: fixed;
		inset: 0;
		background: rgba(0, 0, 0, 0.85);
		display: flex;
		align-items: center;
		justify-content: center;
		z-index: 100;
	}

	.modal {
		position: relative;
		background: #16213e;
		border: 2px solid #6aaa6a;
		border-radius: 10px;
		padding: 1rem;
		line-height: 0;
	}

	.modal-label {
		line-height: normal;
		margin-top: 0.5rem;
		font-size: 0.9rem;
		color: #a0c8a0;
		text-align: center;
	}

	.controls {
		display: flex;
		justify-content: center;
		gap: 2rem;
		margin-top: 1rem;
		line-height: normal;
	}

	.btn {
		width: 52px;
		height: 52px;
		border-radius: 50%;
		background: #2a4a2a;
		border: 2px solid #6aaa6a;
		color: #a0c8a0;
		font-size: 1rem;
		font-weight: bold;
		cursor: pointer;
		transition: background 0.1s, transform 0.1s;
	}

	.btn:hover {
		background: #3a6a3a;
	}

	.btn:active {
		background: #6aaa6a;
		transform: scale(0.93);
	}

	.close {
		position: absolute;
		top: -14px;
		right: -14px;
		width: 28px;
		height: 28px;
		border-radius: 50%;
		background: #6aaa6a;
		color: #000;
		font-size: 1.1rem;
		font-weight: bold;
		border: none;
		cursor: pointer;
		line-height: 28px;
		text-align: center;
		padding: 0;
	}

	.close:hover {
		background: #8aca8a;
	}
</style>
