# front end common imports
from pacman.model.graphs.machine.impl.machine_edge import MachineEdge
from spinn_front_end_common.abstract_models. \
    abstract_provides_n_keys_for_partition import \
    AbstractProvidesNKeysForPartition


class ShallowWaterEdge(MachineEdge):
    """ Used in conjunction with a weather demo vertex
    """

    def __init__(self, pre_vertex, post_vertex, compass, label=None):
        MachineEdge.__init__(
            self, pre_vertex, post_vertex, label=label)
        self._compass = compass

    @property
    def compass(self):
        return self._compass

    def __str__(self):
        return self.__repr__()

    def __repr__(self):
        return "WeatherDemoEdge:{}:{}".format(self._compass, self._label)