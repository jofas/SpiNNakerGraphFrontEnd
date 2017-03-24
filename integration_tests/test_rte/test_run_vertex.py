from pacman.model.resources.resource_container import ResourceContainer
from spinn_front_end_common.abstract_models.abstract_has_associated_binary\
    import AbstractHasAssociatedBinary
from spinn_front_end_common.abstract_models\
    .abstract_generates_data_specification \
    import AbstractGeneratesDataSpecification
from spinn_front_end_common.interface.simulation import simulation_utilities
from spinn_front_end_common.utilities import constants
from pacman.model.graphs.machine.impl.simple_machine_vertex \
    import SimpleMachineVertex


class TestRunVertex(
        SimpleMachineVertex, AbstractHasAssociatedBinary,
        AbstractGeneratesDataSpecification):

    def __init__(self, aplx_file, executable_type):
        SimpleMachineVertex.__init__(self, ResourceContainer())
        self._aplx_file = aplx_file
        self._executable_type = executable_type

    def get_binary_file_name(self):
        return self._aplx_file

    def get_binary_start_type(self):
        return self._executable_type

    def generate_data_specification(self, spec, placement):
        spec.reserve_memory_region(0, constants.SYSTEM_BYTES_REQUIREMENT)
        spec.switch_write_focus(0)
        spec.write_array(
            simulation_utilities.get_simulation_header_array(
                self._aplx_file, 1000, time_scale_factor=1)
        )
        spec.end_specification()