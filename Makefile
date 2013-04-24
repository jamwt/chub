all:
	python setup.py install
dev:
	cd chub && cython chub.pyx
	python setup.py install
