using Avalonia.Controls;
using Avalonia.Interactivity;
using ScalextricDesktopClient.ViewModels;

namespace ScalextricDesktopClient.Views;

public partial class MainWindow : Window
{
    private MainViewModel _vm = null!;

    public MainWindow()
    {
        InitializeComponent();
        _vm = new MainViewModel();
        DataContext = _vm;

        _vm.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(MainViewModel.IsConnected))
                ConnectButton.Content = _vm.IsConnected ? "Disconnect" : "Connect";
        };
    }

    private async void OnConnectClick(object? sender, RoutedEventArgs e)
    {
        if (_vm.IsConnected)
            await _vm.DisconnectAsync();
        else
            await _vm.ConnectAsync();
    }

    private void OnRefreshPorts(object? sender, RoutedEventArgs e)
    {
        _vm.RefreshSerialPorts();
    }

    private void OnBleScan(object? sender, RoutedEventArgs e)
    {
        if (_vm.IsConnected) return;
        _vm.StartBleScan();
    }
}
