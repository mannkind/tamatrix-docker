# Tamatrix
Run your very own Matrix for Tamagotchis, where they are cared for by a benevolent artificial intelligence.

## How to Run
The project is set up as a Docker container. After you've installed [Docker](https://www.docker.com/), simply run:

```bash
# Build the image
docker build -t tamatrix .

# Start a container
docker run -td -p 3000:80 tamatrix
```

Now go to `localhost:3000` in your browser and see your Matrix running!

To run more (or fewer) Tamagotchis, set the `TAMA_COUNT` environment variable (default: 20):

```bash
docker run -td -p 3000:80 -e TAMA_COUNT=5 tamatrix
```

## The ROMs
The Docker setup will start a Tamagotchi emulator for each ROM in the `roms/` directory. Several starters are provided there. If you would like to have more Tamagotchis running, add more ROMs to this folder and increase `TAMA_COUNT` accordingly.

# Background
This is a reimplementation of the codebase created by [Spritesmods](http://spritesmods.com/). The original repository is hosted on a personal git server:

```
git clone http://git.spritesserver.nl/tamatrix.git
```

The details of the original project can be found [here](https://spritesmods.com/?art=tamasingularity).
