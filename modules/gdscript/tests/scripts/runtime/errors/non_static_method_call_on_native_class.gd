# https://github.com/jundotengine/jundot/issues/66675

func example(thing):
	print(thing.has_method("asdf"))

func test():
	example(Node2D)
