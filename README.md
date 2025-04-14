### One-Time Setup

Use following commands to create a Python virtual environment and install the required Python packages.
```
$ python -m venv .venv
$ source .venv/bin/activate
$ pip install -r requirements.txt
```

### Setup

Use following commands to activate the Python virtual environment and add various environment variables.
```
$ source .venv/bin/activate
$ source setup.sh
```

### Compiling ZSim
```
$ cd zsim
$ scons -j4
```
