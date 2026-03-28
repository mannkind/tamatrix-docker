<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import { tamaStore } from '$lib/tamaStore.js';
	import type { Snippet } from 'svelte';

	interface Props {
		children: Snippet;
	}

	let { children }: Props = $props();

	onMount(() => tamaStore.start());
	onDestroy(() => tamaStore.stop());
</script>

{@render children()}

<style>
	:global(*) {
		box-sizing: border-box;
		margin: 0;
		padding: 0;
	}

	:global(body) {
		background: #1a1a2e;
		color: #e0e0e0;
		font-family: system-ui, sans-serif;
		min-height: 100vh;
	}
</style>
