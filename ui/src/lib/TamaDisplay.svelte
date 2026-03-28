<script lang="ts">
	import { onMount } from 'svelte';

	const PIXEL_COLORS: Record<string, string> = {
		A: '#efffe0',
		B: '#A0B090',
		C: '#707058',
		D: '#102000'
	};

	const LCD_W = 48;
	const LCD_H = 32;

	interface Props {
		pixels: string;
		scale?: number;
	}

	let { pixels, scale = 5 }: Props = $props();

	let canvas: HTMLCanvasElement;

	const width = LCD_W * scale;
	const height = LCD_H * scale;
	const pixelSize = scale - 1;

	function draw(ctx: CanvasRenderingContext2D, px: string) {
		let i = 0;
		for (let y = 0; y < LCD_H; y++) {
			for (let x = 0; x < LCD_W; x++) {
				const c = px[i++] ?? 'A';
				ctx.fillStyle = PIXEL_COLORS[c] ?? PIXEL_COLORS.A;
				ctx.fillRect(x * scale, y * scale, pixelSize, pixelSize);
			}
		}
	}

	onMount(() => {
		const ctx = canvas.getContext('2d')!;
		draw(ctx, pixels);
	});

	$effect(() => {
		if (!canvas) return;
		const ctx = canvas.getContext('2d')!;
		draw(ctx, pixels);
	});
</script>

<canvas bind:this={canvas} {width} {height}></canvas>

<style>
	canvas {
		display: block;
		image-rendering: pixelated;
	}
</style>
